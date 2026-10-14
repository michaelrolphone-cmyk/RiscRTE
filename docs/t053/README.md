# T053 Waveshare USB PHY profile

The delivered .034 native profile has USB PHY resource support compiled out.
Enable the existing default-off RISC_ENABLE_USB_PHY feature and retain its linked
marker so product qualification can prove it is present. Native USB remains an
exclusive lease; protocol and storage behavior stay in ordinary providers.
The generic export SDK accessor independently validates the export tail instead
of requiring an unrelated optional sleep extension. No ABI layout changes.
