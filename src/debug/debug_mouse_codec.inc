void WritePoint(ByteWriter& writer, MousePoint point)
{
    writer.Number(point.x);
    writer.Number(point.y);
}

bool ReadFiniteNumber(ByteReader& reader, double& value)
{
    return reader.Number(value) && std::isfinite(value);
}

bool ReadPoint(ByteReader& reader, MousePoint& point)
{
    return ReadFiniteNumber(reader, point.x) && ReadFiniteNumber(reader, point.y);
}

void WriteMouseDelta(ByteWriter& writer, const MouseDelta& delta)
{
    writer.Number(delta.dx);
    writer.Number(delta.dy);
    writer.Number(delta.wheelX);
    writer.Number(delta.wheelY);
}

bool ReadMouseDelta(ByteReader& reader, MouseDelta& delta)
{
    return ReadFiniteNumber(reader, delta.dx) && ReadFiniteNumber(reader, delta.dy)
        && ReadFiniteNumber(reader, delta.wheelX) && ReadFiniteNumber(reader, delta.wheelY);
}

void WriteMouseCycle(ByteWriter& writer, const MouseCycle& cycle)
{
    WriteEnum(writer, cycle.period.type);
    if (cycle.period.type == ExpressionType::Duration) writer.I64(cycle.period.durationValue.nanoseconds);
    else writer.Number(cycle.period.numberValue);
    writer.Number(cycle.progress);
    writer.I64(cycle.elapsedNanoseconds);
    WritePoint(writer, cycle.start);
    WritePoint(writer, cycle.point);
    WritePoint(writer, cycle.displacement);
    writer.Number(cycle.distance);
    writer.U64(cycle.sequence);
}

bool ReadMouseCycle(ByteReader& reader, MouseCycle& cycle)
{
    if (!ReadEnum(reader, cycle.period.type, static_cast<std::uint16_t>(ExpressionType::Duration))) return false;
    if (cycle.period.type == ExpressionType::Duration) {
        if (!reader.I64(cycle.period.durationValue.nanoseconds) || cycle.period.durationValue.nanoseconds < 0) return false;
    } else if ((cycle.period.type != ExpressionType::Number && cycle.period.type != ExpressionType::None)
        || !ReadFiniteNumber(reader, cycle.period.numberValue) || cycle.period.numberValue < 0) return false;
    return ReadFiniteNumber(reader, cycle.progress) && reader.I64(cycle.elapsedNanoseconds)
        && cycle.elapsedNanoseconds >= 0 && ReadPoint(reader, cycle.start) && ReadPoint(reader, cycle.point)
        && ReadPoint(reader, cycle.displacement) && ReadFiniteNumber(reader, cycle.distance)
        && cycle.distance >= 0 && reader.U64(cycle.sequence);
}

void WriteOccurrence(ByteWriter& writer, const MouseOccurrence& occurrence)
{
    writer.U32(occurrence.source.value);
    WriteMouseCycle(writer, occurrence.cycle);
}

bool ReadOccurrence(ByteReader& reader, MouseOccurrence& occurrence)
{
    return reader.U32(occurrence.source.value) && ReadMouseCycle(reader, occurrence.cycle);
}

void WriteMouseSnapshot(ByteWriter& writer, const RuntimeMouseSnapshot& snapshot)
{
    WritePoint(writer, snapshot.mouse.position);
    WriteMouseDelta(writer, snapshot.mouse.delta);
    writer.I64(snapshot.mouse.idleTime.nanoseconds);
    writer.Boolean(snapshot.mouse.moving);
    writer.U32(static_cast<std::uint32_t>(snapshot.sources.size()));
    for (const auto& source : snapshot.sources) {
        WriteMouseCycle(writer, source.current);
        WriteMouseCycle(writer, source.completed);
        writer.Boolean(source.moving);
    }
}

bool ReadMouseSnapshot(ByteReader& reader, RuntimeMouseSnapshot& snapshot)
{
    std::uint32_t count{};
    if (!ReadPoint(reader, snapshot.mouse.position) || !ReadMouseDelta(reader, snapshot.mouse.delta)
        || !reader.I64(snapshot.mouse.idleTime.nanoseconds) || snapshot.mouse.idleTime.nanoseconds < 0
        || !reader.Boolean(snapshot.mouse.moving) || !reader.U32(count)
        || count > reader.Remaining() / 179U) return false;
    snapshot.sources.resize(count);
    for (auto& source : snapshot.sources) {
        if (!ReadMouseCycle(reader, source.current) || !ReadMouseCycle(reader, source.completed)
            || !reader.Boolean(source.moving)) return false;
    }
    return true;
}

bool WriteMouseCapture(ByteWriter& writer, const CaptureStartedPayload& capture)
{
    if (capture.sources.size() != capture.mouse.sources.size()) return false;
    writer.U32(static_cast<std::uint32_t>(capture.sources.size()));
    for (const auto& source : capture.sources) {
        if (source.name.size() > kMaximumDebugTextBytes) return false;
        writer.String(source.name);
        WriteEnum(writer, source.transition);
        WriteEnum(writer, source.periodType);
    }
    WriteMouseSnapshot(writer, capture.mouse);
    return true;
}

bool ReadMouseCapture(ByteReader& reader, CaptureStartedPayload& capture)
{
    std::uint32_t count{};
    if (!reader.U32(count) || count > reader.Remaining() / 6U) return false;
    capture.sources.resize(count);
    for (auto& source : capture.sources) {
        if (!reader.String(source.name) || source.name.empty()
            || !ReadEnum(reader, source.transition, static_cast<std::uint16_t>(EventTransition::HorizontalWheel))
            || source.transition < EventTransition::Move
            || !ReadEnum(reader, source.periodType, static_cast<std::uint16_t>(ExpressionType::Duration))
            || (source.periodType != ExpressionType::Number && source.periodType != ExpressionType::Duration)) return false;
    }
    return ReadMouseSnapshot(reader, capture.mouse) && capture.mouse.sources.size() == capture.sources.size();
}
