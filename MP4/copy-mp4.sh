rm -rf ~/linux-stable/security/mp4
cp -r ~/CS423/MP4/ ~/linux-stable/security/mp4
cd ~/linux-stable
make -j8 bindeb-pkg
cd ~/
sudo update-grub
