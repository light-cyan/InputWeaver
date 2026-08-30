#pragma once

#include "runtime_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace inputweaver {

enum class ArrayIndexStatus : std::uint8_t {
    Valid,
    NonFinite,
    Negative,
    TooLarge,
    OutOfBounds,
};

struct NormalizedArrayIndex final {
    ArrayIndexStatus status{ArrayIndexStatus::Valid};
    std::size_t value{};

    [[nodiscard]] bool Valid() const noexcept {
        return status == ArrayIndexStatus::Valid;
    }
};

[[nodiscard]] NormalizedArrayIndex NormalizeArrayIndex(
    double value,
    std::size_t length) noexcept;

struct ArrayAppendPlan final {
    std::size_t size{};
    std::size_t pageCount{};
    std::size_t directoryCapacity{};
    std::size_t expandedDirectoryCapacity{};
    std::size_t projectedBytes{};
    bool requiresPage{};

    [[nodiscard]] bool operator==(const ArrayAppendPlan&) const noexcept = default;
};

template <typename T, std::size_t PageElementCount = 256U>
class PagedArray final {
public:
    using Page = std::array<T, PageElementCount>;
    using PagePointer = std::unique_ptr<Page>;

    struct PreparedGrowth final {
        PagePointer page;
        std::unique_ptr<PagePointer[]> directory;
    };

    PagedArray() = default;

    explicit PagedArray(std::span<const T> initialValues)
        : size_(initialValues.size())
    {
        pageCount_ = PageCountFor(size_);
        directoryCapacity_ = pageCount_;
        if (directoryCapacity_ != 0U) {
            pages_ = std::make_unique<PagePointer[]>(directoryCapacity_);
        }
        for (std::size_t pageIndex = 0U; pageIndex < pageCount_; ++pageIndex) {
            pages_[pageIndex] = std::make_unique<Page>();
        }
        for (std::size_t index = 0U; index < initialValues.size(); ++index) {
            Element(index) = initialValues[index];
        }
    }

    PagedArray(PagedArray&&) noexcept = default;
    PagedArray& operator=(PagedArray&&) noexcept = default;
    PagedArray(const PagedArray&) = delete;
    PagedArray& operator=(const PagedArray&) = delete;

    [[nodiscard]] std::size_t Size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t AllocatedBytes() const noexcept {
        return pageCount_ * sizeof(Page)
            + directoryCapacity_ * sizeof(PagePointer);
    }

    [[nodiscard]] static std::optional<std::size_t> AllocationBytesForSize(
        std::size_t size) noexcept
    {
        const std::size_t pageCount = PageCountFor(size);
        constexpr std::size_t maximum =
            (std::numeric_limits<std::size_t>::max)();
        if (pageCount > maximum / sizeof(Page)) {
            return std::nullopt;
        }
        const std::size_t pageBytes = pageCount * sizeof(Page);
        if (pageCount > (maximum - pageBytes) / sizeof(PagePointer)) {
            return std::nullopt;
        }
        return pageBytes + pageCount * sizeof(PagePointer);
    }

    [[nodiscard]] const T& At(std::size_t index) const noexcept {
        return Element(index);
    }

    [[nodiscard]] T& At(std::size_t index) noexcept {
        return Element(index);
    }

    [[nodiscard]] std::optional<ArrayAppendPlan> PlanAppend() const noexcept
    {
        if (size_ == (std::numeric_limits<std::size_t>::max)()) {
            return std::nullopt;
        }
        const std::size_t requiredPage = size_ / PageElementCount;
        if (requiredPage > pageCount_) {
            return std::nullopt;
        }
        const bool requiresPage = requiredPage == pageCount_;
        std::size_t expandedDirectoryCapacity = 0U;
        if (requiresPage && pageCount_ == directoryCapacity_) {
            if (directoryCapacity_
                > (std::numeric_limits<std::size_t>::max)() / 2U) {
                return std::nullopt;
            }
            expandedDirectoryCapacity = directoryCapacity_ == 0U
                ? 1U
                : directoryCapacity_ * 2U;
        }
        const std::size_t projectedPageCount = pageCount_ + (requiresPage ? 1U : 0U);
        const std::size_t projectedDirectoryCapacity = expandedDirectoryCapacity != 0U
            ? expandedDirectoryCapacity
            : directoryCapacity_;
        constexpr std::size_t maximum =
            (std::numeric_limits<std::size_t>::max)();
        if (projectedPageCount > maximum / sizeof(Page)) {
            return std::nullopt;
        }
        const std::size_t pageBytes = projectedPageCount * sizeof(Page);
        if (projectedDirectoryCapacity
            > (maximum - pageBytes) / sizeof(PagePointer)) {
            return std::nullopt;
        }
        return ArrayAppendPlan{
            size_,
            pageCount_,
            directoryCapacity_,
            expandedDirectoryCapacity,
            pageBytes + projectedDirectoryCapacity * sizeof(PagePointer),
            requiresPage};
    }

    [[nodiscard]] std::optional<PreparedGrowth> PrepareAppend(
        const ArrayAppendPlan& plan) const noexcept
    {
        if (PlanAppend() != plan) {
            return std::nullopt;
        }
        try {
            PreparedGrowth growth{};
            if (plan.requiresPage) {
                growth.page = std::make_unique<Page>();
            }
            if (plan.expandedDirectoryCapacity != 0U) {
                growth.directory = std::make_unique<PagePointer[]>(
                    plan.expandedDirectoryCapacity);
            }
            return growth;
        } catch (const std::bad_alloc&) {
            return std::nullopt;
        }
    }

    [[nodiscard]] bool Append(
        T value,
        const ArrayAppendPlan& plan,
        PreparedGrowth&& growth) noexcept
    {
        if (PlanAppend() != plan) {
            return false;
        }
        if (plan.requiresPage) {
            if (!growth.page) {
                return false;
            }
            if (plan.expandedDirectoryCapacity != 0U) {
                if (!growth.directory) {
                    return false;
                }
                for (std::size_t index = 0U; index < pageCount_; ++index) {
                    growth.directory[index] = std::move(pages_[index]);
                }
                pages_ = std::move(growth.directory);
                directoryCapacity_ = plan.expandedDirectoryCapacity;
            }
            pages_[pageCount_] = std::move(growth.page);
            ++pageCount_;
        }
        Element(size_) = value;
        ++size_;
        return true;
    }

    [[nodiscard]] bool Pop(T& value) noexcept
    {
        if (size_ == 0U) {
            return false;
        }
        --size_;
        value = Element(size_);
        return true;
    }

    void Clear() noexcept
    {
        size_ = 0U;
    }

private:
    [[nodiscard]] static constexpr std::size_t PageCountFor(
        std::size_t size) noexcept
    {
        return size == 0U ? 0U : 1U + ((size - 1U) / PageElementCount);
    }

    [[nodiscard]] T& Element(std::size_t index) noexcept
    {
        return (*pages_[index / PageElementCount])[index % PageElementCount];
    }

    [[nodiscard]] const T& Element(std::size_t index) const noexcept
    {
        return (*pages_[index / PageElementCount])[index % PageElementCount];
    }

    std::unique_ptr<PagePointer[]> pages_;
    std::size_t pageCount_{};
    std::size_t directoryCapacity_{};
    std::size_t size_{};
};

class RuntimeArrayStorage final {
public:
    using StateArray = PagedArray<std::uint8_t>;
    using NumberArray = PagedArray<double>;
    using PreparedAppend = std::variant<
        StateArray::PreparedGrowth,
        NumberArray::PreparedGrowth>;

    RuntimeArrayStorage(
        ArrayElementType elementType,
        std::span<const std::uint8_t> initialStates,
        std::span<const double> initialNumbers);

    RuntimeArrayStorage(RuntimeArrayStorage&&) noexcept = default;
    RuntimeArrayStorage& operator=(RuntimeArrayStorage&&) noexcept = default;
    RuntimeArrayStorage(const RuntimeArrayStorage&) = delete;
    RuntimeArrayStorage& operator=(const RuntimeArrayStorage&) = delete;

    [[nodiscard]] ArrayElementType ElementType() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] std::size_t AllocatedBytes() const noexcept;
    [[nodiscard]] bool Read(std::size_t index, RuntimeValue& value) const noexcept;
    [[nodiscard]] bool Set(std::size_t index, const RuntimeValue& value) noexcept;
    [[nodiscard]] bool Toggle(std::size_t index) noexcept;
    [[nodiscard]] std::optional<ArrayAppendPlan> PlanAppend() const noexcept;
    [[nodiscard]] std::optional<PreparedAppend> PrepareAppend(
        const ArrayAppendPlan& plan) const noexcept;
    [[nodiscard]] bool Append(
        const RuntimeValue& value,
        const ArrayAppendPlan& plan,
        PreparedAppend&& prepared) noexcept;
    [[nodiscard]] bool Pop(RuntimeValue& value) noexcept;
    void Clear() noexcept;

private:
    std::variant<StateArray, NumberArray> storage_;
};

} // namespace inputweaver
