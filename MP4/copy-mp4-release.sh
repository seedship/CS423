rm -rf ~/linux-stable-release/security/mp4
cp -r ~/CS423/MP4/ ~/linux-stable-release/security/mp4
cd ~/
rm *.deb
rm *.changes
cd ~/linux-stable-release
make -j`getconf _NPROCESSORS_ONLN` bindeb-pkg
cd ~/
sudo dpkg -i linux-image-4.4.0*
sudo dpkg -i linux-headers-4.4.0*
