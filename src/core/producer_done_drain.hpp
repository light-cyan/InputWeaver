#pragma once

namespace inputweaver {

enum class ProducerDrainWaitResult : unsigned char {
    Continue,
    ProducerDone
};

template <typename DrainAvailableFunction, typename WaitFunction>
void DrainUntilProducerDone(
    DrainAvailableFunction&& drainAvailable,
    WaitFunction&& waitForWorkOrProducerDone)
    noexcept(noexcept(drainAvailable()) && noexcept(waitForWorkOrProducerDone()))
{
    for (;;) {
        drainAvailable();
        if (waitForWorkOrProducerDone() == ProducerDrainWaitResult::ProducerDone) {
            break;
        }
    }
    drainAvailable();
}

}  // namespace inputweaver
