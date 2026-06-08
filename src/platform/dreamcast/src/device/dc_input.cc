// SPDX-FileCopyrightText: Copyright 2026 The NanoBoyAdvance Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device/dc_input.hh"

#if NBA_DC_HAS_KOS
#include <dc/video.h>
#endif

namespace nba {

void DCInput::ClearKeys(CoreBase& core) {
  for(int i = 0; i < static_cast<int>(Key::Count); i++) {
    core.SetKeyStatus(static_cast<Key>(i), false);
  }
}

#if NBA_DC_HAS_KOS
auto DCInput::ReadControllerState() -> cont_state_t* {
  maple_device_t* cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
  if(!cont) {
    return nullptr;
  }

  return (cont_state_t*)maple_dev_status(cont);
}

auto DCInput::ButtonPressed(uint32 current, uint32 previous, uint32 mask) -> bool {
  return (current & mask) && !(previous & mask);
}

static auto IsInvalidControllerState(cont_state_t const* state) -> bool {
  // Flycast can report every digital button bit set when no usable keyboard
  // or controller mapping is active.  Treat that impossible menu state as
  // disconnected input so the runtime does not see the exit combo held.
  return state && state->buttons == 0xFFFF;
}
#endif

auto DCInput::PollInput(CoreBase& core, DCGameplayRequest& request) -> bool {
  request = {};

#if NBA_DC_HAS_KOS
  if(save_state_cooldown_ > 0) {
    save_state_cooldown_--;
  }
  if(load_state_cooldown_ > 0) {
    load_state_cooldown_--;
  }

  cont_state_t* state = ReadControllerState();
  if(!state || IsInvalidControllerState(state)) {
    ClearKeys(core);
    exit_combo_frames_ = 0;
    pause_menu_frames_ = 0;
    previous_shoulder_combo_ = 0;
    return false;
  }

  core.SetKeyStatus(Key::A,      state->buttons & CONT_A);
  core.SetKeyStatus(Key::B,      state->buttons & CONT_B);
  core.SetKeyStatus(Key::L,      state->buttons & CONT_X);
  core.SetKeyStatus(Key::R,      state->buttons & CONT_Y);
  core.SetKeyStatus(Key::Start,  state->buttons & CONT_START);
  core.SetKeyStatus(Key::Select, state->buttons & CONT_D);

  bool left  = (state->buttons & CONT_DPAD_LEFT)  || (state->joyx < -kAnalogDeadZone);
  bool right = (state->buttons & CONT_DPAD_RIGHT) || (state->joyx >  kAnalogDeadZone);
  bool up    = (state->buttons & CONT_DPAD_UP)    || (state->joyy < -kAnalogDeadZone);
  bool down  = (state->buttons & CONT_DPAD_DOWN)  || (state->joyy >  kAnalogDeadZone);

  core.SetKeyStatus(Key::Left,  left && !right);
  core.SetKeyStatus(Key::Right, right && !left);
  core.SetKeyStatus(Key::Up,    up && !down);
  core.SetKeyStatus(Key::Down,  down && !up);

  const uint32 exit_combo = CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y;
  if((state->buttons & exit_combo) == exit_combo) {
    exit_combo_frames_++;
    pause_menu_frames_ = 0;
  } else {
    exit_combo_frames_ = 0;
  }

  const uint32 pause_combo = CONT_START | CONT_B;
  if((state->buttons & pause_combo) == pause_combo && exit_combo_frames_ == 0) {
    pause_menu_frames_++;
    if(pause_menu_frames_ == kPauseMenuDebounceFrames) {
      request.open_pause_menu = true;
    }
  } else if((state->buttons & pause_combo) != pause_combo) {
    pause_menu_frames_ = 0;
  }

  // gpSPDC-style shoulder shortcuts: hold GBA L+R (Dreamcast X+Y) with Start
  // to save and Select to load the active save-state slot.
  const uint32 shoulder_combo = CONT_X | CONT_Y;
  const bool shoulders_held = (state->buttons & shoulder_combo) == shoulder_combo;
  if(shoulders_held) {
    if((state->buttons & CONT_START) && save_state_cooldown_ == 0) {
      request.save_state = true;
      save_state_cooldown_ = kSaveStateDebounceFrames;
    } else if((state->buttons & CONT_D) && load_state_cooldown_ == 0) {
      request.load_state = true;
      load_state_cooldown_ = kSaveStateDebounceFrames;
    }

    const uint32 slot_change = state->buttons & (CONT_DPAD_LEFT | CONT_DPAD_RIGHT);
    if(slot_change && slot_change != previous_shoulder_combo_) {
      if(state->buttons & CONT_DPAD_LEFT) {
        request.save_slot_delta = -1;
      } else if(state->buttons & CONT_DPAD_RIGHT) {
        request.save_slot_delta = 1;
      }
    }
    previous_shoulder_combo_ = slot_change;
  } else {
    previous_shoulder_combo_ = 0;
  }

  return exit_combo_frames_ >= kExitDebounceFrames;
#elif NBA_DC_HAS_SDL_MENU
  auto event = SDL_Event{};
  bool exit_requested = false;
  while(SDL_PollEvent(&event)) {
    if(event.type == SDL_EVENT_QUIT) {
      exit_requested = true;
      continue;
    }

    if(event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
      continue;
    }

    switch(event.key.scancode) {
      case SDL_SCANCODE_ESCAPE:
        request.open_pause_menu = true;
        break;
      case SDL_SCANCODE_F5:
        request.save_state = true;
        break;
      case SDL_SCANCODE_F8:
        request.load_state = true;
        break;
      case SDL_SCANCODE_PAGEUP:
        request.save_slot_delta = 1;
        break;
      case SDL_SCANCODE_PAGEDOWN:
        request.save_slot_delta = -1;
        break;
      case SDL_SCANCODE_Q:
        exit_requested = true;
        break;
      default:
        break;
    }
  }

  const bool* keys = SDL_GetKeyboardState(nullptr);
  core.SetKeyStatus(Key::A,      keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_RETURN]);
  core.SetKeyStatus(Key::B,      keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_BACKSPACE]);
  core.SetKeyStatus(Key::L,      keys[SDL_SCANCODE_A]);
  core.SetKeyStatus(Key::R,      keys[SDL_SCANCODE_S]);
  core.SetKeyStatus(Key::Start,  keys[SDL_SCANCODE_RETURN]);
  core.SetKeyStatus(Key::Select, keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_BACKSPACE]);
  core.SetKeyStatus(Key::Left,   keys[SDL_SCANCODE_LEFT]);
  core.SetKeyStatus(Key::Right,  keys[SDL_SCANCODE_RIGHT]);
  core.SetKeyStatus(Key::Up,     keys[SDL_SCANCODE_UP]);
  core.SetKeyStatus(Key::Down,   keys[SDL_SCANCODE_DOWN]);
  return exit_requested;
#else
  (void)core;
  (void)request;
  return false;
#endif
}

auto DCInput::IsExitHintActive() const -> bool {
  return exit_combo_frames_ >= kExitHintFrames &&
         exit_combo_frames_ < kExitDebounceFrames;
}

auto DCInput::PollMenu(DCMenuInput& menu) -> void {
#if NBA_DC_HAS_KOS
  menu = {};

  cont_state_t* state = ReadControllerState();
  if(!state || IsInvalidControllerState(state)) {
    previous_buttons_ = 0xFFFF;
    previous_joyx_ = 0;
    previous_joyy_ = 0;
    return;
  }

  const uint32 current = state->buttons;
  menu.up = ButtonPressed(current, previous_buttons_, CONT_DPAD_UP);
  menu.down = ButtonPressed(current, previous_buttons_, CONT_DPAD_DOWN);
  menu.left = ButtonPressed(current, previous_buttons_, CONT_DPAD_LEFT);
  menu.right = ButtonPressed(current, previous_buttons_, CONT_DPAD_RIGHT);
  menu.confirm = ButtonPressed(current, previous_buttons_, CONT_A);
  menu.cancel = ButtonPressed(current, previous_buttons_, CONT_B);
  menu.settings = ButtonPressed(current, previous_buttons_, CONT_Y);
  menu.start = ButtonPressed(current, previous_buttons_, CONT_START);

  const bool analog_up = state->joyy < -kAnalogDeadZone;
  const bool analog_down = state->joyy > kAnalogDeadZone;
  const bool analog_left = state->joyx < -kAnalogDeadZone;
  const bool analog_right = state->joyx > kAnalogDeadZone;
  const bool prev_analog_up = previous_joyy_ < -kAnalogDeadZone;
  const bool prev_analog_down = previous_joyy_ > kAnalogDeadZone;
  const bool prev_analog_left = previous_joyx_ < -kAnalogDeadZone;
  const bool prev_analog_right = previous_joyx_ > kAnalogDeadZone;

  menu.up |= analog_up && !prev_analog_up;
  menu.down |= analog_down && !prev_analog_down;
  menu.left |= analog_left && !prev_analog_left;
  menu.right |= analog_right && !prev_analog_right;

  previous_buttons_ = current;
  previous_joyx_ = state->joyx;
  previous_joyy_ = state->joyy;
#elif NBA_DC_HAS_SDL_MENU
  menu = {};

  auto event = SDL_Event{};
  while(SDL_PollEvent(&event)) {
    if(event.type == SDL_EVENT_QUIT) {
      menu.start = true;
      menu.cancel = true;
      continue;
    }

    if(event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
      continue;
    }

    switch(event.key.scancode) {
      case SDL_SCANCODE_UP:        menu.up = true; break;
      case SDL_SCANCODE_DOWN:      menu.down = true; break;
      case SDL_SCANCODE_LEFT:      menu.left = true; break;
      case SDL_SCANCODE_RIGHT:     menu.right = true; break;
      case SDL_SCANCODE_RETURN:
      case SDL_SCANCODE_SPACE:
      case SDL_SCANCODE_Z:         menu.confirm = true; break;
      case SDL_SCANCODE_ESCAPE:
      case SDL_SCANCODE_BACKSPACE:
      case SDL_SCANCODE_X:         menu.cancel = true; break;
      case SDL_SCANCODE_Y:
      case SDL_SCANCODE_S:         menu.settings = true; break;
      case SDL_SCANCODE_F1:
      case SDL_SCANCODE_TAB:       menu.start = true; break;
      default:                     break;
    }
  }
#else
  (void)menu;
#endif
}

auto DCInput::WaitForButton(Button button) -> void {
#if NBA_DC_HAS_KOS
  uint32 mask = CONT_START;
  if(button == Button::Start) {
    mask = CONT_START;
  }

  while(true) {
    cont_state_t* state = ReadControllerState();
    if(state && !IsInvalidControllerState(state) && (state->buttons & mask)) {
      break;
    }

    vid_waitvbl();
  }

  while(true) {
    cont_state_t* state = ReadControllerState();
    if(!state || IsInvalidControllerState(state) || !(state->buttons & mask)) {
      break;
    }

    vid_waitvbl();
  }
#else
#if NBA_DC_HAS_SDL_MENU
  auto event = SDL_Event{};
  bool pressed = false;
  while(!pressed && SDL_WaitEvent(&event)) {
    if(event.type == SDL_EVENT_QUIT) {
      break;
    }
    if(event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
      continue;
    }
    if(button == Button::Start &&
        (event.key.scancode == SDL_SCANCODE_RETURN ||
         event.key.scancode == SDL_SCANCODE_SPACE ||
         event.key.scancode == SDL_SCANCODE_F1)) {
      pressed = true;
    }
  }
#else
  (void)button;
#endif
#endif
}

} // namespace nba
