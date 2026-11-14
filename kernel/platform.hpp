#pragma once

namespace alacre::platform {

void init_console();
void put_char(char character);
const char *name();
[[noreturn]] void halt();

} // namespace alacre::platform
