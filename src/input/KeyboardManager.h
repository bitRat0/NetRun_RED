#pragma once

enum class InputAction
{
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back
};

class KeyboardManager
{
public:
    void begin();
    InputAction readAction() const;
    // Returns the newly pressed printable key without exposing Cardputer
    // keycodes to UI code. Navigation keys remain InputAction values.
    char readCharacter() const;
    bool consumeBackspace() const;

private:
    mutable char pendingCharacter_ = '\0';
    mutable bool pendingBackspace_ = false;
};
