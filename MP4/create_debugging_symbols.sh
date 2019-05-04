rm -rf ~/linux-stable/security/mp4
cp -r ~/CS423/MP4/ ~/linux-stable/security/mp4
cd ~/linux-stable
make -j`getconf _NPROCESSORS_ONLN`
cp security/mp4/mp4.o ~/CS423/MP4/
cd ~/
