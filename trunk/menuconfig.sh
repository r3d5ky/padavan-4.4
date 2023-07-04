cp -f ./configs/boards/$1/kernel-4.4.x.config ./linux-4.4.x/.config
cd linux-4.4.x && make menuconfig ARCH=mips
echo Replace board config with new file?
read answer
if [ "$answer" != "${answer#[Yy]}" ] ;then 
    mv -f .config ../configs/boards/$1/kernel-4.4.x.config
else
    rm .config
fi
