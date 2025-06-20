#pragma once

inline size_t AlignTo(size_t offset, size_t alignment) {
	return (offset + alignment - 1) & ~(alignment - 1);
}