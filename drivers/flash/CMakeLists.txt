# SPDX-License-Identifier: Apache-2.0

if (CONFIG_SPI_FLASH_AT45D)

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SPI_FLASH_AT45D spi_flash_at45d.c)

endif () # CONFIG_SPI_FLASH_AT45D
