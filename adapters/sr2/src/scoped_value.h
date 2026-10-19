#pragma once

// Sets a variable (usually a thread slot) for the life of the object and puts the earlier value back.
template <typename T>
class ScopedValue {
public:
    ScopedValue(T& slot, T value) : slot_(slot), previous_(slot) { slot_ = value; }
    ~ScopedValue() { slot_ = previous_; }
    ScopedValue(const ScopedValue&) = delete;
    ScopedValue& operator=(const ScopedValue&) = delete;

private:
    T& slot_;
    T previous_;
};
