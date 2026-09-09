#!/bin/bash -e
cmake -S /tmp/tic80-source -B /tmp/tic80-build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_PRO=ON -DBUILD_STATIC=ON \
    -DBUILD_SDLGPU=ON -DPREFER_SYSTEM_LIBRARIES=ON \
    -DBUILD_WITH_ALL=ON
cmake --build /tmp/tic80-build --target tic80 -j2
install -m 755 /tmp/tic80-build/bin/tic80 /usr/local/bin/tic80
strip /usr/local/bin/tic80
ldd /usr/local/bin/tic80
if ldd /usr/local/bin/tic80 | grep -q 'not found'; then exit 1; fi
xvfb-run -a -s '-screen 0 1280x720x24' python3 /tmp/tic80-smoke.py
rm -rf /tmp/tic80-source /tmp/tic80-build

chmod 755 /usr/local/lib/tic80/{launch,session,import-wifi}
install -d /etc/systemd/system/getty@tty1.service.d /etc/systemd/journald.conf.d
install -m 644 /usr/local/lib/tic80/autologin.conf /etc/systemd/system/getty@tty1.service.d/autologin.conf
install -m 644 /usr/local/lib/tic80/journal.conf /etc/systemd/journald.conf.d/tic80.conf
install -m 644 /usr/local/lib/tic80/tic80-wifi.service /etc/systemd/system/
install -m 644 /usr/local/lib/tic80/profile /home/tic80/.bash_profile
install -m 440 /usr/local/lib/tic80/poweroff.sudoers /etc/sudoers.d/tic80-poweroff
visudo -cf /etc/sudoers.d/tic80-poweroff
install -d /etc/wireplumber/wireplumber.conf.d
install -m 644 /usr/local/lib/tic80/usb-audio.conf /etc/wireplumber/wireplumber.conf.d/51-tic80-usb.conf
install -d -o tic80 -g tic80 /home/tic80/cartridges
chown tic80:tic80 /home/tic80/.bash_profile
usermod -L tic80
install -d -m 700 -o tic80 -g tic80 /home/tic80/.ssh
install -m 600 -o tic80 -g tic80 /usr/local/lib/tic80/authorized_keys /home/tic80/.ssh/authorized_keys
cmp /home/tic80/.ssh/authorized_keys /usr/local/lib/tic80/authorized_keys
rm /usr/local/lib/tic80/authorized_keys
install -m 644 /usr/local/lib/tic80/sshd.conf /etc/ssh/sshd_config.d/00-tic80.conf
systemctl enable ssh.service
install -d /run/sshd
ssh-keygen -q -t ed25519 -N '' -f /run/tic80-test-hostkey
sshd -t -h /run/tic80-test-hostkey
sshd -T -h /run/tic80-test-hostkey | grep -qx 'passwordauthentication no'
sshd -T -h /run/tic80-test-hostkey | grep -qx 'permitrootlogin no'
rm /run/tic80-test-hostkey /run/tic80-test-hostkey.pub
su -s /bin/sh tic80 -c 'sudo -n /usr/bin/true'
# Remove the superseded HTTP logger when resuming an older build volume.
systemctl disable tic80-logs.service 2>/dev/null || true
rm -f /etc/systemd/system/tic80-logs.service
systemctl enable tic80-wifi.service avahi-daemon.service
systemctl disable NetworkManager-wait-online.service
systemctl set-default multi-user.target
# Do not ship a flashing utility that changes an already-configured EEPROM.
systemctl disable rpi-eeprom-update.service || true
systemctl mask rpi-eeprom-update.service
