/*
 * brickd
 * Copyright (C) 2012-2013 Matthias Bolte <matthias@tinkerforge.com>
 *
 * array.c: Array specific functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"

#include "utils.h"

// sets errno on error
int array_create(Array *array, int reserved, int size, int relocatable) {
	reserved = GROW_ALLOCATION(reserved);

	array->allocated = 0;
	array->count = 0;
	array->size = size;
	array->relocatable = relocatable;
	array->bytes = calloc(reserved, relocatable ? size : (int)sizeof(void *));

	if (array->bytes == NULL) {
		errno = ENOMEM;

		return -1;
	}

	array->allocated = reserved;

	return 0;
}

void array_destroy(Array *array, FreeFunction function) {
	int i;
	void *item;

	if (function != NULL) {
		for (i = 0; i < array->count; ++i) {
			item = array_get(array, i);

			function(item);

			if (!array->relocatable) {
				free(item);
			}
		}
	} else if (!array->relocatable) {
		for (i = 0; i < array->count; ++i) {
			free(array_get(array, i));
		}
	}

	free(array->bytes);
}

// sets errno on error
int array_reserve(Array *array, int count) {
	int size = array->relocatable ? array->size : (int)sizeof(void *);
	uint8_t *bytes;

	if (array->allocated >= count) {
		return 0;
	}

	count = GROW_ALLOCATION(count);
	bytes = realloc(array->bytes, count * size);

	if (bytes == NULL) {
		errno = ENOMEM;

		return -1;
	}

	array->allocated = count;
	array->bytes = bytes;

	return 0;
}

// sets errno on error
int array_resize(Array *array, int count, FreeFunction function) {
	int rc;
	int i;
	void *item;

	if (array->count < count) {
		rc = array_reserve(array, count);

		if (rc < 0) {
			return rc;
		}
	} else if (array->count > count) {
		if (function != NULL) {
			for (i = count; i < array->count; ++i) {
				item = array_get(array, i);

				function(item);

				if (!array->relocatable) {
					free(item);
				}
			}
		} else if (!array->relocatable) {
			for (i = count; i < array->count; ++i) {
				free(array_get(array, i));
			}
		}
	}

	array->count = count;

	return 0;
}

// sets errno on error
void *array_append(Array *array) {
	void *item;

	if (array_reserve(array, array->count + 1) < 0) {
		return NULL;
	}

	++array->count;

	if (array->relocatable) {
		item = array_get(array, array->count - 1);

		memset(item, 0, array->size);
	} else {
		item = calloc(1, array->size);

		if (item == NULL) {
			--array->count;

			errno = ENOMEM;

			return NULL;
		}

		*(void **)(array->bytes + sizeof(void *) * (array->count - 1)) = item;
	}

	return item;
}

void array_remove(Array *array, int i, FreeFunction function) {
	void *item = array_get(array, i);
	int size = array->relocatable ? array->size : (int)sizeof(void *);
	int tail;

	if (item == NULL) {
		return;
	}

	if (function != NULL) {
		function(item);
	}

	if (!array->relocatable) {
		free(item);
	}

	tail = (array->count - i - 1) * size;

	if (tail > 0) {
		memmove(array->bytes + size * i, array->bytes + size * (i + 1), tail);
	}

	memset(array->bytes + size * (array->count - 1), 0, size);

	--array->count;
}

void *array_get(Array *array, int i) {
	if (i >= array->count) {
		return NULL;
	}

	if (array->relocatable) {
		return array->bytes + array->size * i;
	} else {
		return *(void **)(array->bytes + sizeof(void *) * i);
	}
}
