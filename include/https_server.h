#pragma once

#include <stddef.h>
#include <stdint.h>

// Takes ownership of cert and key. Both lengths include the terminating NUL.
bool httpsBegin(uint8_t* cert, size_t certLen, uint8_t* key, size_t keyLen);
