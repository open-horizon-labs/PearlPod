# FTP receiver provenance

Adapted from nopnop2002/esp-idf-ftpServer commit 6e24d85c53ae7adbd59e9616444b54f2c8d2bdae, itself derived from LoBo/Pycom FTP code. MIT notices remain in the source and LICENSE. Confirm the cloned revision when updating this note.

PearlPod changes: existing SD mount only, managed root `/sdcard/music/.pearl`, port 2121, anonymous protocol responses without password checks or login state, explicit start/close in a bounded sync worker, larger bounded transfer buffer, bounded path joins, traversal rejection, command-parameter termination fix, date-buffer bound fix, stable listing path across command restoration, bounded listing writes without interior-pointer realloc, safe allocation-failure cleanup, and host socket shims for sanitizer/interoperability testing. No upstream mounting, WiFi setup or permanent FTP task is used.
