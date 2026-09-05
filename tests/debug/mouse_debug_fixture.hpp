#pragma once

#include "debug/debug_protocol.hpp"

namespace inputweaver::test {

inline debug::CaptureStartedPayload MouseDebugCapture()
{
    debug::CaptureStartedPayload capture;
    capture.captureUnixTimeMilliseconds = 1'725'000'000'000LL;
    capture.meters = {{"path", EventTransition::Move, ExpressionType::Number},
        {"pulse", EventTransition::Move, ExpressionType::Duration},
        {"vertical", EventTransition::Wheel, ExpressionType::Number}};
    capture.mouse.mouse = {{-73, 20}, {0, 0, 0, 0.25}, {12'000'000}, true};
    capture.mouse.meters.resize(3);
    auto& path = capture.mouse.meters[0];
    path.current.period.type = ExpressionType::Number;
    path.current.period.numberValue = 24;
    path.current.start = {-76, 20};
    path.current.point = {-73, 20};
    path.current.displacement = {3, 0};
    path.current.distance = path.current.progress = 3;
    path.moving = true;
    path.completed = path.current;
    path.completed.start = {-100, 20};
    path.completed.point = {-76, 20};
    path.completed.displacement.x = 24;
    path.completed.distance = path.completed.progress = 24;
    path.completed.sequence = 12;
    auto& pulse = capture.mouse.meters[1];
    pulse.current = path.current;
    pulse.current.period.type = ExpressionType::Duration;
    pulse.current.period.numberValue = 0;
    pulse.current.period.durationValue.nanoseconds = 100'000'000;
    pulse.current.elapsedNanoseconds = 20'000'000;
    pulse.moving = true;
    auto& wheel = capture.mouse.meters[2];
    wheel.current.period.type = ExpressionType::Number;
    wheel.current.period.numberValue = 1;
    wheel.current.progress = -0.25;
    wheel.current.point = {-73, 20};
    return capture;
}

} // namespace inputweaver::test
