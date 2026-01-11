# Disable default low power implementation, use lp_sleep.c style instead
WIRELESS_LPWR_STOP_ENABLE = no
ENTRY_STOP_MODE_ENABLE = yes

include keyboards/shortcut/wireless/wireless.mk
