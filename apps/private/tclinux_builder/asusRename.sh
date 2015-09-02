#!/bin/bash
cp generateMD5.sh $PROJECT_DIR/images
cd $PROJECT_DIR/images
version=`cat $APP_ETC_SCRIPT_DIR/fwver.conf`
product_name=`sed -n 's/product_name=//p' $PROFILE_DIR/mi.conf | tr -d '\r'`
filename=$product_name"_"$version
mv tclinux.bin $filename".trx"
mv tclinux_allinone $filename".bin"

if [ -f $APP_PRIVATE_DIR/cfg_manager/private.pem ];
then
	openssl sha1 -sign $APP_PRIVATE_DIR/cfg_manager/private.pem -out $filename"_rsa.zip" $filename".trx"
	echo "File exists."
else
	echo "File does not exist."
fi

chmod 777 generateMD5.sh
./generateMD5.sh $product_name $version > md5Number.txt
rm generateMD5.sh
