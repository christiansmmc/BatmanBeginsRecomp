// Holding a button for a second ends a pre-rendered video. Real-time scenes never come here: see docs/TECHNICAL.md, "Videos".
#pragma once

class MovieSkip {
public:
    // a button already down when the video starts counts only after it is let go
    explicit MovieSkip(bool held_at_start = false) : waiting_for_release_(held_at_start) {}

    // true when the video should end
    bool Update(bool held, double seconds);
    // from 0 to 1, for the notice on the screen
    float Progress() const;

private:
    bool waiting_for_release_;
    double held_seconds_ = 0.0;
};
