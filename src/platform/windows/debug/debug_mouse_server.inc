    bool PopulateMouseCapture(debug::CaptureStartedPayload& capture) const
    {
        for (const auto& source : program->EventSources()) {
            capture.sources.push_back({program->Strings()[source.name.value], source.transition,
                program->Expressions()[source.period.value].resultType});
        }
        return callbacks.readMouseSnapshot.invoke ? callbacks.readMouseSnapshot.Invoke(capture.mouse)
            : capture.sources.empty();
    }

    bool SendMouseRecord(HANDLE pipe, const ProducerRecord& record, std::uint64_t& sequence) const
    {
        const auto& event = record.runtime;
        const bool completion = event.kind == RuntimeDebugEventKind::MouseCycleCompleted;
        auto message = MakeMessage(completion ? debug::MessageKind::MouseCycleCompleted
            : debug::MessageKind::ExecutionSourceSelected, record.captureEpoch, record.captureTimeNanoseconds, sequence);
        if (completion) message.mouseCycleCompleted = {event.triggerInputSequence, event.occurrence};
        else message.executionSourceSelected = {event.executionMarker, event.occurrence};
        return WriteMessage(pipe, message);
    }

    bool SendMouseState(HANDLE pipe, std::uint64_t epoch, std::uint64_t& sequence, debug::Message& message) const
    {
        if (!captureActive.load(std::memory_order_acquire) || !callbacks.readMouseSnapshot.Invoke(message.mouseState)
            || epoch != captureEpoch.load(std::memory_order_acquire)
            || !captureActive.load(std::memory_order_acquire)) return true;
        message.header = MakeMessage(debug::MessageKind::MouseState, epoch, CaptureTimeNanoseconds(), sequence).header;
        return WriteMessage(pipe, message);
    }
