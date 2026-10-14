#pragma once

namespace pixo
{

/** Pd's [line] as Rx used it ("$1 300" → line): each new target starts a
    linear ramp from the current value, over a fixed time. Evaluated per
    frame instead of Pd's 20 ms grain.

    One deliberate difference: the first target is taken immediately rather
    than ramping up from 0, so the picture doesn't zoom in at start-up. */
class LineRamp
{
public:
    explicit LineRamp (double rampMsIn = 0.0) : rampMs (rampMsIn) {}

    void setTarget (float target, double nowMs)
    {
        if (! started)
        {
            from = to = target;
            started = true;
            return;
        }

        if (target == to)
            return;

        from = valueAt (nowMs);
        to = target;
        startMs = nowMs;
    }

    float valueAt (double nowMs) const
    {
        if (rampMs <= 0.0 || nowMs >= startMs + rampMs)
            return to;

        const auto progress = static_cast<float> ((nowMs - startMs) / rampMs);
        return from + (to - from) * (progress < 0.0f ? 0.0f : progress);
    }

    /** setTarget() then valueAt(), the usual per-frame call. */
    float update (float target, double nowMs)
    {
        setTarget (target, nowMs);
        return valueAt (nowMs);
    }

private:
    double rampMs;
    double startMs = 0.0;
    float from = 0.0f;
    float to = 0.0f;
    bool started = false;
};

} // namespace pixo
