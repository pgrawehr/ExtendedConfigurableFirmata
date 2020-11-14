#pragma once

// The PC is a von-Neumann-system, so no need to use progmem and the like
#define strlen_P(x) strlen(x)
#define pgm_read_byte(x) (*(x))
