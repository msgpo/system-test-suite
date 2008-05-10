
#Please Setting this...
GTA02_DM_RELEASE_VERSION = ""

BASE_DIR = ${PWD}
GTA02_DM1_DIR = ${BASE_DIR}/gta02-dm1
GTA02_DM1_TARGET = GTA02-P${GTA02_DM_RELEASE_VERSION}.tar.gz
GTA02_DM2_DIR = ${BASE_DIR}/gta02-dm2
GTA02_DM2_TARGET = dm2.bin
GTA02_RELEASE_DIR = ${BASE_DIR}/release_data
GTA02_VERSION_FILE = ${BASE_DIR}/setting_dm_version

BI_KERNEL_VER = 2.6.22.5
DL_KERNEL_SRC = http://www.kernel.org/pub/linux/kernel/v2.6/linux-${BI_KERNEL_VER}.tar.bz2 

export GTA02_VERSION_FILE ;

GTA02_CROSS = "arm-angstrom-linux-gnueabi-"

all: verify_version_setup gta02-dm1-dummy gta02-dm2

# test #########

.PHONY: verify_version_setup
verify_version_setup:
	@if [ -z ${GTA02_DM_RELEASE_VERSION} ]; then \
		echo "" && \
		echo "\tERROR!!! Please Setting GTA02_DM_RELEASE_VERSION in Makefile" && \
		echo "" && \
		false; \
	fi
	@echo ${GTA02_DM_RELEASE_VERSION} > ${GTA02_VERSION_FILE}
	@echo ${GTA02_DM_RELEASE_VERSION} > ./gta02-dm2/RELEASE_STRING

.PHONY: dump_setting
dump_setting: 
	@echo ""
	@echo "GTA02_DM_RELEASE_VERSION = "${GTA02_DM_RELEASE_VERSION}
	@echo ""
	@echo "BASE_DIR = "${BASE_DIR}
	@echo "GTA02_DM1_DIR = "${GTA02_DM1_DIR}
	@echo "GTA02_DM1_TARGET = "${GTA02_DM1_TARGET}
	@echo "GTA02_DM2_DIR = "${GTA02_DM2_DIR}
	@echo "GTA02_DM2_TARGET = "${GTA02_DM2_TARGET}
	@echo ""
	@echo "PATH = "${PATH}
	@echo ""

# all ##########
.PHONY: gta02-dm1-build-kernel-include-rootfs
gta02-dm1-build-kernel-include-rootfs:
	if [ ! -d ${GTA02_DM1_DIR}/tmp ]; then \
		mkdir ${GTA02_DM1_DIR}/tmp; \
	fi && \
	if [ ! -e ${GTA02_DM1_DIR}/tmp/rootfs/bin/busybox ]; then \
		tar -xzf ${GTA02_DM1_DIR}/rootfs/rootfs.tar.gz \
			-C ${GTA02_DM1_DIR}/tmp; \
	fi && \
	cd ${GTA02_DM1_DIR}/user_space && make && \
	cp main libgsmd-tool gsmd \
		${GTA02_DM1_DIR}/tmp/rootfs/bin && \
	cd ${GTA02_DM1_DIR}/tmp && \
	if [ ! -e ./linux-2.6.22.5.tar.bz2 ]; then \
		wget ${DL_KERNEL_SRC}; \
	fi && \
	if [ ! -d ./svn_kernel ]; then \
		svn co http://svn.openmoko.org/trunk/src/target/kernel && \
		mv -f kernel svn_kernel; \
	fi
	cd ${GTA02_DM1_DIR}/tmp/svn_kernel && \
	if [ ! -e ./scripts/build.modify_for_dm1 ]; then \
		cd ${GTA02_DM1_DIR}/tmp/svn_kernel && \
		sed  's/# KERNEL=${BI_KERNEL_VER}/KERNEL=${BI_KERNEL_VER}/g' \
			./scripts/build > ./scripts/build.enable_kernel_ver && \
		sed 's/# KERNELSRC_DIR=\/wherever/KERNELSRC_DIR=$$\{PWD\}\/..\//g' \
			./scripts/build.enable_kernel_ver > \
			./scripts/build.enable_kernel_ver.detect_path && \
		sed 's/SVN\/trunk\/src\/target\/kernel/\{PWD\}\/../g' \
			./scripts/build.enable_kernel_ver.detect_path > \
			./scripts/build.enable_kernel_ver.detect_path.modify_patch_path && \
		sed 's/^make ARCH=arm/# make ARCH=arm/g' \
			./scripts/build.enable_kernel_ver.detect_path.modify_patch_path > \
			./scripts/build.modify_for_dm1 && \
		chmod 755 ./scripts/build.modify_for_dm1; \
	fi
	cd ${GTA02_DM1_DIR}/tmp/svn_kernel && \
	if [ ! -e ${GTA02_DM1_DIR}/tmp/svn_kernel/linux-${BI_KERNEL_VER}/first-patch-done ]; then \
		./scripts/build.modify_for_dm1 && \
		echo "done" > ${GTA02_DM1_DIR}/tmp/svn_kernel/linux-${BI_KERNEL_VER}/first-patch-done; \
	fi
	cd ${GTA02_DM1_DIR}/tmp/svn_kernel && \
	if [ ! -d linux-${BI_KERNEL_VER} ]; then \
		tar -xjf ${GTA02_DM1_DIR}/tmp/linux-${BI_KERNEL_VER}.tar.bz2; \
	fi && \
	cd ${GTA02_DM1_DIR}/tmp/svn_kernel/linux-${BI_KERNEL_VER} && \
	if [ ! -e ./.dm1_patched_done ]; then \
		rm patches && \
		cd ${GTA02_DM1_DIR}/kernel_module && \
		./setup-kernel.sh ../tmp/svn_kernel/linux-${BI_KERNEL_VER} && \
		cd ${GTA02_DM1_DIR}/tmp/svn_kernel/linux-${BI_KERNEL_VER} && \
		echo "${GTA02_DM1_DIR}/tmp/rootfs" > pwd.txt &&  sed 's/\//\\\//g' pwd.txt > pwd.txt.done && \
		sed "s/\/gta02\/pbe\/rootfs/`cat pwd.txt.done`/g" .config \
		> config.modifyed && mv config.modifyed .config && \
		rm pwd.txt pwd.txt.done && \
		echo "done" > ./.dm1_patched_done; \
	fi
	cd ${GTA02_DM1_DIR}/tmp/svn_kernel/linux-${BI_KERNEL_VER} && \
	export PATH=${PATH}:${GTA02_DM1_DIR}/tmp/u-boot/tools && \
	if [ ! -e .load_oldconfig_done ]; then \
		make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- oldconfig && \
		touch .load_oldconfig_done; \
	fi && \
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- uImage -j 2 && \
	if [ ! -e arch/arm/boot/uImage ]; then \
		echo "" && \
		echo "##### ${PWD}/arch/arm/boot/uImage is not Built Done #####" && \
		echo "" && \
		false; \
	fi
	@echo "" 
	@echo " ##### kernel and include rootfs is Ready!! #####" 
	@echo ""

.PHONY: gta02-dm1-build-nor-uboot
gta02-dm1-build-nor-uboot:
	@if [ ! -d ${GTA02_DM1_DIR}/tmp ]; then \
		mkdir ${GTA02_DM1_DIR}/tmp; \
	fi
	cd ${GTA02_DM1_DIR}/tmp && \
	if [ ! -e ./devirginator/.svn/entries ]; then \
		svn co http://svn.openmoko.org/trunk/src/host/devirginator; \
	fi && \
	if [ ! -e ./splash/.svn/entries ]; then \
		svn co http://svn.openmoko.org/trunk/src/host/splash; \
	fi && \
	if [ ! -e ./u-boot/.git/HEAD ]; then \
		git clone git://git.openmoko.org/git/u-boot.git && \
		cd u-boot && git checkout origin/stable && \
		cd ${GTA02_DM1_DIR}/tmp; \
	fi && \
	if [ ! -e ./devirginator/System_boot.png ]; then \
		cd ./devirginator && \
		wget http://wiki.openmoko.org/images/c/c2/System_boot.png; \
	fi && \
	cd ${GTA02_DM1_DIR}/tmp/u-boot && \
	make ARCH=arm CROSS_COMPILE=${GTA02_CROSS} gta02v5_config && \
	make ARCH=arm CROSS_COMPILE=${GTA02_CROSS} u-boot.udfu -j 2 && \
	cd ${GTA02_DM1_DIR}/tmp/devirginator && \
	./mknor -D QUIET -s ./System_boot.png \
		-o ../u-boot/u-boot.udfu.nor ../u-boot/u-boot.udfu
	@echo "" 
	@echo " ##### u-boot.udfu.nor for NOR Ready!! #####" 
	@echo ""

.PHONY: gta02-dm1-dummy
gta02-dm1-dummy: 
	@echo "##### build ${GTA02_DM1_TARGET} #####"
	@echo ""
	@if [ ! -e ${GTA02_VERSION_FILE} ]; then \
		echo "" && \
		echo "\tERROR!!! Please Setting GTA02_DM_RELEASE_VERSION in Makefile" && \
		echo "" && \
		false; \
	fi
	@if [ ! -d ${GTA02_DM1_DIR}/tmp ]; then \
		mkdir ${GTA02_DM1_DIR}/tmp; \
	fi
	@rm -fr ${GTA02_DM1_DIR}/tmp/* && \
	cd ${GTA02_DM1_DIR}/tmp && \
	tar -xzf ../GTA02-P-version.tar.gz && \
	echo ${GTA02_DM_RELEASE_VERSION} > GTA02-DM1/version && \
	tar -czf ${GTA02_DM1_DIR}/${GTA02_DM1_TARGET} \
		GTA02-DM1 && \
	cd ${BASE_DIR}

.PHONY: gta02-dm2
gta02-dm2: 
	@echo "##### build ${GTA02_DM2_TARGET} #####"
	@if [ ! -e ${GTA02_VERSION_FILE} ]; then \
		echo "" && \
		echo "\tERROR!!! Please Setting GTA02_DM_RELEASE_VERSION in Makefile" && \
		echo "" && \
		false; \
	fi
	@cd ${GTA02_DM2_DIR} && ./build && cd ${BASE_DIR} 


# install ##########
.PHONY: mkdir_release_dir
mkdir_release_dir:
	@if [ ! -d ${GTA02_RELEASE_DIR} ]; then \
		mkdir ${GTA02_RELEASE_DIR}; \
	fi
	@if [ ! -d ${GTA02_RELEASE_DIR}/gta02-dm2 ]; then \
		mkdir ${GTA02_RELEASE_DIR}/gta02-dm2; \
	fi

.PHONY: install-gta02-dm1-dammy
install-gta02-dm1-dammy: mkdir_release_dir
	@if [ -e ${GTA02_DM1_DIR}/${GTA02_DM1_TARGET} ]; then \
		mv ${GTA02_DM1_DIR}/${GTA02_DM1_TARGET} \
			${GTA02_RELEASE_DIR}/${GTA02_DM1_TARGET}; \
	fi

.PHONY: install-gta02-dm2
install-gta02-dm2: mkdir_release_dir
	@if [ -e ${GTA02_DM2_DIR}/staging/usr/bin/${GTA02_DM2_TARGET} ]; then \
		cp -frp ${GTA02_DM2_DIR}/staging/* \
			${GTA02_RELEASE_DIR}/gta02-dm2 && \
		cp ${GTA02_DM2_DIR}/staging/usr/bin/${GTA02_DM2_TARGET} \
			${GTA02_RELEASE_DIR}/${GTA02_DM2_TARGET}; \
	fi


install: mkdir_release_dir install-gta02-dm1-dammy install-gta02-dm2

# clean ##########
.PHONY: gta02-dm1-build-kernel-include-rootfs-clean
gta02-dm1-build-kernel-include-rootfs-clean:
	cd ${GTA02_DM1_DIR}/user_space && make clean && \
	if [ -d ${GTA02_DM1_DIR}/tmp/rootfs ]; then \
		rm -fr ${GTA02_DM1_DIR}/tmp/rootfs; \
	fi
	cd ${GTA02_DM1_DIR}/tmp/svn_kernel/linux-${BI_KERNEL_VER} && \
	make ARCH=arm CROSS_COMPILE=arm-angstrom-linux-gnueabi- clean

.PHONY: gta02-dm1-build-nor-uboot-clean
gta02-dm1-build-nor-uboot-clean:
	if [ -e ${GTA02_DM1_DIR}/tmp/u-boot/u-boot ]; then \
		cd ${GTA02_DM1_DIR}/tmp/u-boot && \
		make ARCH=arm CROSS_COMPILE=${GTA02_CROSS} distclean && \
		cd - ; \
	fi 
	if [ -e ${GTA02_DM1_DIR}/tmp/u-boot/u-boot.udfu ]; then \
		rm ${GTA02_DM1_DIR}/tmp/u-boot/u-boot.udfu; \
	fi
	if [ -e ${GTA02_DM1_DIR}/tmp/u-boot/u-boot.udfu.nor ]; then \
		rm ${GTA02_DM1_DIR}/tmp/u-boot/u-boot.udfu.nor; \
	fi


.PHONY: clean-release-data
clean-release-data:
	@if [ -d ${GTA02_RELEASE_DIR} ]; then \
		rm -fr ${GTA02_RELEASE_DIR}/* && \
		rmdir ${GTA02_RELEASE_DIR}; \
	fi

.PHONY: clean-gta02-dm1-dammy
clean-gta02-dm1-dammy:
	@echo "##### clean dm1 #####"
	@echo ""
	#@if [ -d ${GTA02_DM1_DIR}/tmp ]; then \
		rm -fr ${GTA02_DM1_DIR}/tmp; \
	fi

.PHONY: clean-gta02-dm2
clean-gta02-dm2:
	@echo "##### clean ${GTA02_DM2_TARGET} #####"
	@cd ${GTA02_DM2_DIR} && make clean && rm -fr staging \
		&& cd ${BASE_DIR} 

clean: clean-release-data clean-gta02-dm1-dammy clean-gta02-dm2
	@if [ -e ${GTA02_VERSION_FILE} ]; then \
		rm ${GTA02_VERSION_FILE}; \
	fi

