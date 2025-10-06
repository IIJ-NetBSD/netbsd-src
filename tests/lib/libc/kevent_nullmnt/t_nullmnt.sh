# find where everything lives

curdir=$(pwd)
helper=$(atf_get_srcdir)/h_nullmnt

# helper function to check for kernel support
have_nullfs_support()
{
	for t in $( sysctl -n vfs.generic.fstypes )
	do
		case "$t" in
		  null)	return 0;;
		esac
	done

	return 1
}

# common test body
#    $1 = directory of file to monitor
#    $2 = directory of file to update/modify

nullmnt_common()
{
	if ! have_nullfs_support; then
		atf_skip "nullfs not supported"
	fi

	mkdir ${curdir}/lower_dir
	mkdir ${curdir}/upper_dir
	mount -t null ${curdir}/lower_dir ${curdir}/upper_dir || \
		atf_fail "could not mount nullfs"
	rm -f ${curdir}/lower_dir/afile
	touch ${curdir}/lower_dir/afile

	atf_check -e ignore -o ignore -s exit:0		\
		${helper} ${curdir}/${1}/afile ${curdir}/${2}/afile
}

nullmnt_common_cleanup()
{
	curdir=$(pwd)
	umount ${curdir}/upper_dir
	rm -rf ${curdir}/lower_dir ${curdir}/upper_dir
}

atf_test_case nullmnt_upper_lower cleanup
nullmnt_upper_lower_head()
{
	atf_set "descr" "ensure upper fs events seen on lower fs"
	atf_set "require.user" "root"
}
nullmnt_upper_lower_body()
{
	nullmnt_common lower_dir upper_dir
} 
nullmnt_upper_lower_cleanup()
{
	nullmnt_common_cleanup
}

atf_test_case nullmnt_upper_upper cleanup
nullmnt_upper_upper_head()
{
	atf_set "descr" "ensure upper fs events seen on upper fs"
	atf_set "require.user" "root"
}
nullmnt_upper_upper_body()
{
	nullmnt_common upper_dir upper_dir
} 
nullmnt_upper_upper_cleanup()
{
	nullmnt_common_cleanup
}
atf_test_case nullmnt_lower_upper cleanup
nullmnt_lower_upper_head()
{
	atf_set "descr" "ensure lower fs events seen on upper fs"
	atf_set "require.user" "root"
}
nullmnt_lower_upper_body()
{
	nullmnt_common upper_dir lower_dir
} 
nullmnt_lower_upper_cleanup()
{
	nullmnt_common_cleanup
}

atf_test_case nullmnt_lower_lower cleanup
nullmnt_lower_lower_head()
{
	atf_set "descr" "ensure lower fs events seen on lower fs"
	atf_set "require.user" "root"
}
nullmnt_lower_lower_body()
{
	nullmnt_common lower_dir lower_dir
} 
nullmnt_lower_lower_cleanup()
{
	nullmnt_common_cleanup
}

atf_init_test_cases()
{
	atf_add_test_case nullmnt_upper_upper
	atf_add_test_case nullmnt_upper_lower
	atf_add_test_case nullmnt_lower_upper
	atf_add_test_case nullmnt_lower_lower
}
