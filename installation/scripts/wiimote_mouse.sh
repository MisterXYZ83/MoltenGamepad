#!/bin/bash
rm /home/mves/dev/mg.sock
/home/mves/dev/MoltenGamepad/moltengamepad --daemon --pidfile /var/run/moltengamepad.pid --make-socket --socket-path /home/mves/dev/mg.sock -c /home/mves/.config/wiimote_mouse
#/home/mves/dev/MoltenGamepad/moltengamepad --make-socket --socket-path /home/mves/dev/mg.sock -c /home/mves/.config/wiimote_mouse

