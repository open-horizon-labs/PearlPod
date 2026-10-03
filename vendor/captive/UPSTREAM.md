# Captive portal provenance

Wildcard DNS, DHCP DNS advertisement, 404 HTTP redirection and deferred initial scan are ported/adapted from `tdongle-tailnet-firmware/main/portal.c` at ce0e172 (MIT notice alongside this file). The lifecycle uses the acknowledged-stop pattern examined in `hiphi-repos/roon-knob-integration/tough_app/main/dns_server.c` at e1872dfa; its probe behavior supplies dedicated iOS HTML and Android redirect handlers. No auth, password generation, token or pairing code was imported.

PearlPod adaptations: validated bounded DNS questions, empty AAAA/HTTPS replies, PSRAM packet buffers, 3 KiB stack, receive timeout, socket ownership until stop acknowledgement, setup-only DNS, browser-activity lease renewal, and no initial radio scan while a phone is joining.
