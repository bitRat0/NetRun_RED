#include "KeyboardManager.h"

#include <M5Cardputer.h>

namespace
{
bool containsCharacter(const Keyboard_Class::KeysState& keys, char expected)
{
    for (const char value : keys.word)
        if (value == expected) return true;
    return false;
}

bool containsHidCode(const Keyboard_Class::KeysState& keys, uint8_t expected)
{
    for (const uint8_t value : keys.hid_keys)
        if (value == expected) return true;
    return false;
}
}

void KeyboardManager::begin()
{
    // The keyboard is initialized by M5Cardputer.begin().
}

InputAction KeyboardManager::readAction() const
{
    M5Cardputer.update();
    pendingCharacter_ = '\0';
    pendingBackspace_ = false;

    if (!M5Cardputer.Keyboard.isChange() ||
        !M5Cardputer.Keyboard.isPressed())
    {
        return InputAction::None;
    }

    const Keyboard_Class::KeysState& keys = M5Cardputer.Keyboard.keysState();
    pendingBackspace_ = keys.backspace;
    for (const char value : keys.word)
        if (value >= 32 && value <= 126) { pendingCharacter_ = value; break; }

    // Cardputer arrows are the Fn layer of ; , . / (HID 52, 50, 51, 4F).
    // Accept the physical base keys as navigation too, so no Fn chord is required.
    if (keys.up || containsHidCode(keys, KEY_UP) || containsCharacter(keys, ';'))
        return InputAction::Up;
    if (keys.down || containsHidCode(keys, KEY_DOWN) || containsCharacter(keys, '.'))
        return InputAction::Down;
    if (keys.left || containsHidCode(keys, KEY_LEFT) || containsCharacter(keys, ','))
        return InputAction::Left;
    if (keys.right || containsHidCode(keys, KEY_RIGHT) || containsCharacter(keys, '/'))
        return InputAction::Right;
    if (keys.enter) return InputAction::Confirm;
    if (keys.esc || keys.backspace) return InputAction::Back;

    return InputAction::None;
}

char KeyboardManager::readCharacter() const
{
    const char result = pendingCharacter_;
    pendingCharacter_ = '\0';
    return result;
}

bool KeyboardManager::consumeBackspace() const
{
    const bool result = pendingBackspace_;
    pendingBackspace_ = false;
    return result;
}
