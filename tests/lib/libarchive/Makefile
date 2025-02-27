# $NetBSD: Makefile,v 1.16 2024/11/21 20:14:07 riastradh Exp $

NOMAN=

.include <bsd.own.mk>
LIBARCHIVE=${NETBSDSRCDIR}/external/bsd/libarchive/dist

TESTSDIR=	${TESTSBASE}/lib/libarchive

BINDIR=                 ${TESTSDIR}
PROGS+=	h_libarchive
TESTS_SH+=	t_libarchive

CPPFLAGS+=-I${LIBARCHIVE}/test_utils -I${LIBARCHIVE}/libarchive -I.
CPPFLAGS+=-I${LIBARCHIVE}/libarchive/test -I${LIBARCHIVE}/../include

.PATH: ${LIBARCHIVE}/libarchive/test ${LIBARCHIVE}/test_utils

DPADD+=	${LIBARCHIVE} ${LIBEXPAT} ${LIBBZ2} ${LIBLZMA} ${LIBZ} \
	${LIBCRYPTO} ${LIBPTHREAD}
LDADD+=	-Wl,--push-state,-Bstatic,-larchive,--pop-state	# uses libarchive guts
LDADD+=	-lexpat -lbz2 -llzma -lz -lcrypto -lpthread

.include "${NETBSDSRCDIR}/external/bsd/zstd/Makefile.zstd"
PROGDPLIBS+=	${ZSTDDPLIBS}	# needed by libarchive

SRCS.h_libarchive= \
read_open_memory.c \
test_acl_nfs4.c \
test_acl_pax.c \
test_acl_platform_nfs4.c \
test_acl_platform_posix1e.c \
test_acl_posix1e.c \
test_acl_text.c \
test_archive_api_feature.c \
test_archive_clear_error.c \
test_archive_cmdline.c \
test_archive_digest.c \
test_archive_getdate.c \
test_archive_match_owner.c \
test_archive_match_path.c \
test_archive_match_time.c \
test_archive_pathmatch.c \
test_archive_read_add_passphrase.c \
test_archive_read_close_twice.c \
test_archive_read_close_twice_open_fd.c \
test_archive_read_close_twice_open_filename.c \
test_archive_read_multiple_data_objects.c \
test_archive_read_next_header_empty.c \
test_archive_read_next_header_raw.c \
test_archive_read_open2.c \
test_archive_read_set_filter_option.c \
test_archive_read_set_format_option.c \
test_archive_read_set_option.c \
test_archive_read_set_options.c \
test_archive_read_support.c \
test_archive_set_error.c \
test_archive_string.c \
test_archive_string_conversion.c \
test_archive_write_add_filter_by_name.c \
test_archive_write_set_filter_option.c \
test_archive_write_set_format_by_name.c \
test_archive_write_set_format_filter_by_ext.c \
test_archive_write_set_format_option.c \
test_archive_write_set_option.c \
test_archive_write_set_options.c \
test_archive_write_set_passphrase.c \
test_bad_fd.c \
test_compat_bzip2.c \
test_compat_cpio.c \
test_compat_gtar.c \
test_compat_gzip.c \
test_compat_lz4.c \
test_compat_lzip.c \
test_compat_lzma.c \
test_compat_lzop.c \
test_compat_mac.c \
test_compat_perl_archive_tar.c \
test_compat_plexus_archiver_tar.c \
test_compat_solaris_pax_sparse.c \
test_compat_solaris_tar_acl.c \
test_compat_star_acl.c \
test_compat_tar_hardlink.c \
test_compat_uudecode.c \
test_compat_uudecode_large.c \
test_compat_xz.c \
test_compat_zip.c \
test_compat_zstd.c \
test_empty_write.c \
test_entry.c \
test_entry_strmode.c \
test_extattr_freebsd.c \
test_filter_count.c \
test_fuzz.c \
test_gnutar_filename_encoding.c \
test_link_resolver.c \
test_main.c \
test_open_failure.c \
test_open_fd.c \
test_open_file.c \
test_open_filename.c \
test_pax_filename_encoding.c \
test_read_data_large.c \
test_read_disk.c \
test_read_disk_directory_traversals.c \
test_read_disk_entry_from_file.c \
test_read_extract.c \
test_read_file_nonexistent.c \
test_read_filter_compress.c \
test_read_filter_grzip.c \
test_read_filter_lrzip.c \
test_read_filter_lzop.c \
test_read_filter_lzop_multiple_parts.c \
test_read_filter_program.c \
test_read_filter_program_signature.c \
test_read_filter_uudecode.c \
test_read_format_7zip.c \
test_read_format_7zip_encryption_data.c \
test_read_format_7zip_encryption_header.c \
test_read_format_7zip_encryption_partially.c \
test_read_format_7zip_malformed.c \
test_read_format_ar.c \
test_read_format_cab.c \
test_read_format_cab_filename.c \
test_read_format_cpio_afio.c \
test_read_format_cpio_bin.c \
test_read_format_cpio_bin_Z.c \
test_read_format_cpio_bin_be.c \
test_read_format_cpio_bin_bz2.c \
test_read_format_cpio_bin_gz.c \
test_read_format_cpio_bin_le.c \
test_read_format_cpio_bin_lzip.c \
test_read_format_cpio_bin_lzma.c \
test_read_format_cpio_bin_xz.c \
test_read_format_cpio_filename.c \
test_read_format_cpio_odc.c \
test_read_format_cpio_svr4_bzip2_rpm.c \
test_read_format_cpio_svr4_gzip.c \
test_read_format_cpio_svr4_gzip_rpm.c \
test_read_format_cpio_svr4c_Z.c \
test_read_format_empty.c \
test_read_format_gtar_filename.c \
test_read_format_gtar_gz.c \
test_read_format_gtar_lzma.c \
test_read_format_gtar_sparse.c \
test_read_format_gtar_sparse_skip_entry.c \
test_read_format_iso_Z.c \
test_read_format_iso_multi_extent.c \
test_read_format_iso_xorriso.c \
test_read_format_isojoliet_bz2.c \
test_read_format_isojoliet_long.c \
test_read_format_isojoliet_rr.c \
test_read_format_isojoliet_versioned.c \
test_read_format_isorr_bz2.c \
test_read_format_isorr_ce.c \
test_read_format_isorr_new_bz2.c \
test_read_format_isorr_rr_moved.c \
test_read_format_isozisofs_bz2.c \
test_read_format_lha.c \
test_read_format_lha_bugfix_0.c \
test_read_format_lha_filename.c \
test_read_format_mtree.c \
test_read_format_mtree_crash747.c \
test_read_format_pax_bz2.c \
test_read_format_rar.c \
test_read_format_rar5.c \
test_read_format_rar_encryption_data.c \
test_read_format_rar_encryption_header.c \
test_read_format_rar_encryption_partially.c \
test_read_format_rar_invalid1.c \
test_read_format_raw.c \
test_read_format_tar.c \
test_read_format_tar_concatenated.c \
test_read_format_tar_empty_filename.c \
test_read_format_tar_empty_pax.c \
test_read_format_tar_empty_with_gnulabel.c \
test_read_format_tar_filename.c \
test_read_format_tbz.c \
test_read_format_tgz.c \
test_read_format_tlz.c \
test_read_format_txz.c \
test_read_format_tz.c \
test_read_format_ustar_filename.c \
test_read_format_warc.c \
test_read_format_xar.c \
test_read_format_zip.c \
test_read_format_zip_7075_utf8_paths.c \
test_read_format_zip_comment_stored.c \
test_read_format_zip_encryption_data.c \
test_read_format_zip_encryption_header.c \
test_read_format_zip_encryption_partially.c \
test_read_format_zip_extra_padding.c \
test_read_format_zip_filename.c \
test_read_format_zip_high_compression.c \
test_read_format_zip_jar.c \
test_read_format_zip_mac_metadata.c \
test_read_format_zip_malformed.c \
test_read_format_zip_msdos.c \
test_read_format_zip_nested.c \
test_read_format_zip_nofiletype.c \
test_read_format_zip_padded.c \
test_read_format_zip_sfx.c \
test_read_format_zip_traditional_encryption_data.c \
test_read_format_zip_winzip_aes.c \
test_read_format_zip_winzip_aes_large.c \
test_read_format_zip_with_invalid_traditional_eocd.c \
test_read_format_zip_zip64.c \
test_read_large.c \
test_read_pax_truncated.c \
test_read_position.c \
test_read_set_format.c \
test_read_too_many_filters.c \
test_read_truncated.c \
test_read_truncated_filter.c \
test_sparse_basic.c \
test_tar_filenames.c \
test_tar_large.c \
test_ustar_filename_encoding.c \
test_ustar_filenames.c \
test_utils.c \
test_warn_missing_hardlink_target.c \
test_write_disk.c \
test_write_disk_appledouble.c \
test_write_disk_failures.c \
test_write_disk_hardlink.c \
test_write_disk_hfs_compression.c \
test_write_disk_lookup.c \
test_write_disk_mac_metadata.c \
test_write_disk_no_hfs_compression.c \
test_write_disk_perms.c \
test_write_disk_secure.c \
test_write_disk_secure744.c \
test_write_disk_secure745.c \
test_write_disk_secure746.c \
test_write_disk_sparse.c \
test_write_disk_symlink.c \
test_write_disk_times.c \
test_write_filter_b64encode.c \
test_write_filter_bzip2.c \
test_write_filter_compress.c \
test_write_filter_gzip.c \
test_write_filter_gzip_timestamp.c \
test_write_filter_lrzip.c \
test_write_filter_lz4.c \
test_write_filter_lzip.c \
test_write_filter_lzma.c \
test_write_filter_lzop.c \
test_write_filter_program.c \
test_write_filter_uuencode.c \
test_write_filter_xz.c \
test_write_filter_zstd.c \
test_write_format_7zip.c \
test_write_format_7zip_empty.c \
test_write_format_7zip_large.c \
test_write_format_ar.c \
test_write_format_cpio.c \
test_write_format_cpio_empty.c \
test_write_format_cpio_newc.c \
test_write_format_cpio_odc.c \
test_write_format_gnutar.c \
test_write_format_gnutar_filenames.c \
test_write_format_iso9660.c \
test_write_format_iso9660_boot.c \
test_write_format_iso9660_empty.c \
test_write_format_iso9660_filename.c \
test_write_format_iso9660_zisofs.c \
test_write_format_mtree.c \
test_write_format_mtree_absolute_path.c \
test_write_format_mtree_classic.c \
test_write_format_mtree_classic_indent.c \
test_write_format_mtree_fflags.c \
test_write_format_mtree_no_separator.c \
test_write_format_mtree_quoted_filename.c \
test_write_format_pax.c \
test_write_format_raw.c \
test_write_format_raw_b64.c \
test_write_format_shar_empty.c \
test_write_format_tar.c \
test_write_format_tar_empty.c \
test_write_format_tar_sparse.c \
test_write_format_tar_ustar.c \
test_write_format_tar_v7tar.c \
test_write_format_warc.c \
test_write_format_warc_empty.c \
test_write_format_xar.c \
test_write_format_xar_empty.c \
test_write_format_zip.c \
test_write_format_zip_compression_store.c \
test_write_format_zip_empty.c \
test_write_format_zip_empty_zip64.c \
test_write_format_zip_file.c \
test_write_format_zip_file_zip64.c \
test_write_format_zip_large.c \
test_write_format_zip_zip64.c \
test_write_open_memory.c \
test_write_read_format_zip.c \
test_xattr_platform.c \
test_zip_filename_encoding.c

FILESDIR=       ${TESTSDIR}
FILES=\
test_acl_pax_nfs4.tar.uu  \
test_acl_pax_posix1e.tar.uu  \
test_archive_string_conversion.txt.Z.uu  \
test_compat_bzip2_1.tbz.uu  \
test_compat_bzip2_2.tbz.uu  \
test_compat_cpio_1.cpio.uu  \
test_compat_gtar_1.tar.uu  \
test_compat_gtar_2.tar.uu  \
test_compat_gzip_1.tgz.uu  \
test_compat_gzip_2.tgz.uu  \
test_compat_lz4_1.tar.lz4.uu  \
test_compat_lz4_2.tar.lz4.uu  \
test_compat_lz4_3.tar.lz4.uu  \
test_compat_lz4_B4.tar.lz4.uu  \
test_compat_lz4_B4BD.tar.lz4.uu  \
test_compat_lz4_B4BDBX.tar.lz4.uu  \
test_compat_lz4_B5.tar.lz4.uu  \
test_compat_lz4_B5BD.tar.lz4.uu  \
test_compat_lz4_B6.tar.lz4.uu  \
test_compat_lz4_B6BD.tar.lz4.uu  \
test_compat_lz4_B7.tar.lz4.uu  \
test_compat_lz4_B7BD.tar.lz4.uu  \
test_compat_lzip_1.tlz.uu  \
test_compat_lzip_2.tlz.uu  \
test_compat_lzip_3.lz.uu  \
test_compat_lzip_4.tlz.uu  \
test_compat_lzma_1.tlz.uu  \
test_compat_lzma_2.tlz.uu  \
test_compat_lzma_3.tlz.uu  \
test_compat_lzop_1.tar.lzo.uu  \
test_compat_lzop_2.tar.lzo.uu  \
test_compat_lzop_3.tar.lzo.uu  \
test_compat_mac-1.tar.Z.uu  \
test_compat_mac-2.tar.Z.uu  \
test_compat_perl_archive_tar.tar.uu  \
test_compat_plexus_archiver_tar.tar.uu  \
test_compat_solaris_pax_sparse_1.pax.Z.uu  \
test_compat_solaris_pax_sparse_2.pax.Z.uu  \
test_compat_solaris_tar_acl.tar.uu  \
test_compat_star_acl_nfs4.tar.uu  \
test_compat_star_acl_posix1e.tar.uu  \
test_compat_tar_directory_1.tar.uu  \
test_compat_tar_hardlink_1.tar.uu  \
test_compat_uudecode_large.tar.Z.uu  \
test_compat_xz_1.txz.uu  \
test_compat_zip_1.zip.uu  \
test_compat_zip_2.zip.uu  \
test_compat_zip_3.zip.uu  \
test_compat_zip_4.zip.uu  \
test_compat_zip_5.zip.uu  \
test_compat_zip_6.zip.uu  \
test_compat_zip_7.xps.uu  \
test_compat_zip_8.zip.uu  \
test_compat_zstd_1.tar.zst.uu  \
test_compat_zstd_1.tar.zst.uu \
test_compat_zstd_2.tar.zst.uu \
test_fuzz.cab.uu  \
test_fuzz.lzh.uu  \
test_fuzz_1.iso.Z.uu  \
test_pax_filename_encoding.tar.uu  \
test_rar_multivolume_multiple_files.part1.rar.uu  \
test_rar_multivolume_multiple_files.part2.rar.uu  \
test_rar_multivolume_multiple_files.part3.rar.uu  \
test_rar_multivolume_multiple_files.part4.rar.uu  \
test_rar_multivolume_multiple_files.part5.rar.uu  \
test_rar_multivolume_multiple_files.part6.rar.uu  \
test_rar_multivolume_single_file.part1.rar.uu  \
test_rar_multivolume_single_file.part2.rar.uu  \
test_rar_multivolume_single_file.part3.rar.uu  \
test_rar_multivolume_uncompressed_files.part01.rar.uu  \
test_rar_multivolume_uncompressed_files.part02.rar.uu  \
test_rar_multivolume_uncompressed_files.part03.rar.uu  \
test_rar_multivolume_uncompressed_files.part04.rar.uu  \
test_rar_multivolume_uncompressed_files.part05.rar.uu  \
test_rar_multivolume_uncompressed_files.part06.rar.uu  \
test_rar_multivolume_uncompressed_files.part07.rar.uu  \
test_rar_multivolume_uncompressed_files.part08.rar.uu  \
test_rar_multivolume_uncompressed_files.part09.rar.uu  \
test_rar_multivolume_uncompressed_files.part10.rar.uu  \
test_read_filter_grzip.tar.grz.uu  \
test_read_filter_lrzip.tar.lrz.uu  \
test_read_filter_lzop.tar.lzo.uu  \
test_read_filter_lzop_multiple_parts.tar.lzo.uu  \
test_read_format_7zip_bcj2_bzip2.7z.uu  \
test_read_format_7zip_bcj2_copy_1.7z.uu  \
test_read_format_7zip_bcj2_copy_2.7z.uu  \
test_read_format_7zip_bcj2_copy_lzma.7z.uu  \
test_read_format_7zip_bcj2_deflate.7z.uu  \
test_read_format_7zip_bcj2_lzma1_1.7z.uu  \
test_read_format_7zip_bcj2_lzma1_2.7z.uu  \
test_read_format_7zip_bcj2_lzma2_1.7z.uu  \
test_read_format_7zip_bcj2_lzma2_2.7z.uu  \
test_read_format_7zip_bcj_bzip2.7z.uu  \
test_read_format_7zip_bcj_copy.7z.uu  \
test_read_format_7zip_bcj_deflate.7z.uu  \
test_read_format_7zip_bcj_lzma1.7z.uu  \
test_read_format_7zip_bcj_lzma2.7z.uu  \
test_read_format_7zip_bzip2.7z.uu  \
test_read_format_7zip_copy.7z.uu  \
test_read_format_7zip_copy_2.7z.uu  \
test_read_format_7zip_deflate.7z.uu  \
test_read_format_7zip_deflate_arm64.7z.uu  \
test_read_format_7zip_delta4_lzma1.7z.uu  \
test_read_format_7zip_delta4_lzma2.7z.uu  \
test_read_format_7zip_delta_lzma1.7z.uu  \
test_read_format_7zip_delta_lzma2.7z.uu  \
test_read_format_7zip_empty_archive.7z.uu  \
test_read_format_7zip_empty_file.7z.uu  \
test_read_format_7zip_encryption.7z.uu  \
test_read_format_7zip_encryption_header.7z.uu  \
test_read_format_7zip_encryption_partially.7z.uu  \
test_read_format_7zip_extract_second.7z.uu \
test_read_format_7zip_lzma1.7z.uu  \
test_read_format_7zip_lzma1_2.7z.uu  \
test_read_format_7zip_lzma1_lzma2.7z.uu  \
test_read_format_7zip_lzma2.7z.uu \
test_read_format_7zip_lzma2_arm.7z.uu \
test_read_format_7zip_malformed.7z.uu  \
test_read_format_7zip_malformed2.7z.uu  \
test_read_format_7zip_ppmd.7z.uu  \
test_read_format_7zip_solid_zstd.7z.uu \
test_read_format_7zip_symbolic_name.7z.uu  \
test_read_format_7zip_win_attrib.7z.uu \
test_read_format_7zip_zstd.7z.uu \
test_read_format_7zip_zstd_arm.7z.uu \
test_read_format_7zip_zstd_bcj.7z.uu \
test_read_format_7zip_zstd_nobcj.7z.uu \
test_read_format_ar.ar.uu  \
test_read_format_cab_1.cab.uu  \
test_read_format_cab_2.cab.uu  \
test_read_format_cab_3.cab.uu  \
test_read_format_cab_filename_cp932.cab.uu  \
test_read_format_cpio_bin_be.cpio.uu  \
test_read_format_cpio_bin_le.cpio.uu  \
test_read_format_cpio_filename_cp866.cpio.uu  \
test_read_format_cpio_filename_eucjp.cpio.uu  \
test_read_format_cpio_filename_koi8r.cpio.uu  \
test_read_format_cpio_filename_utf8_jp.cpio.uu  \
test_read_format_cpio_filename_utf8_ru.cpio.uu  \
test_read_format_cpio_svr4_bzip2_rpm.rpm.uu  \
test_read_format_cpio_svr4_gzip_rpm.rpm.uu  \
test_read_format_gtar_filename_cp866.tar.Z.uu  \
test_read_format_gtar_filename_eucjp.tar.Z.uu  \
test_read_format_gtar_filename_koi8r.tar.Z.uu  \
test_read_format_gtar_sparse_1_13.tar.uu  \
test_read_format_gtar_sparse_1_17.tar.uu  \
test_read_format_gtar_sparse_1_17_posix00.tar.uu  \
test_read_format_gtar_sparse_1_17_posix01.tar.uu  \
test_read_format_gtar_sparse_1_17_posix10.tar.uu  \
test_read_format_gtar_sparse_1_17_posix10_modified.tar.uu  \
test_read_format_gtar_sparse_skip_entry.tar.Z.uu  \
test_read_format_iso.iso.Z.uu  \
test_read_format_iso_2.iso.Z.uu  \
test_read_format_iso_3.iso.Z.uu \
test_read_format_iso_joliet.iso.Z.uu  \
test_read_format_iso_joliet_by_nero.iso.Z.uu  \
test_read_format_iso_joliet_long.iso.Z.uu  \
test_read_format_iso_joliet_rockridge.iso.Z.uu  \
test_read_format_iso_multi_extent.iso.Z.uu  \
test_read_format_iso_rockridge.iso.Z.uu  \
test_read_format_iso_rockridge_ce.iso.Z.uu  \
test_read_format_iso_rockridge_new.iso.Z.uu  \
test_read_format_iso_rockridge_rr_moved.iso.Z.uu  \
test_read_format_iso_xorriso.iso.Z.uu  \
test_read_format_iso_zisofs.iso.Z.uu  \
test_read_format_lha_bugfix_0.lzh.uu  \
test_read_format_lha_filename_cp932.lzh.uu  \
test_read_format_lha_header0.lzh.uu  \
test_read_format_lha_header1.lzh.uu  \
test_read_format_lha_header2.lzh.uu  \
test_read_format_lha_header3.lzh.uu  \
test_read_format_lha_lh0.lzh.uu  \
test_read_format_lha_lh6.lzh.uu  \
test_read_format_lha_lh7.lzh.uu  \
test_read_format_lha_withjunk.lzh.uu  \
test_read_format_mtree.mtree.uu  \
test_read_format_mtree_crash747.mtree.bz2.uu  \
test_read_format_mtree_nomagic.mtree.uu  \
test_read_format_mtree_nomagic2.mtree.uu  \
test_read_format_mtree_nomagic3.mtree.uu  \
test_read_format_mtree_noprint.mtree.uu  \
test_read_format_rar.rar.uu  \
test_read_format_rar5_arm.rar.uu  \
test_read_format_rar5_arm_filter_on_window_boundary.rar.uu \
test_read_format_rar5_bad_window_sz_in_mltarc_file.rar.uu \
test_read_format_rar5_blake2.rar.uu  \
test_read_format_rar5_block_size_is_too_small.rar.uu \
test_read_format_rar5_compressed.rar.uu  \
test_read_format_rar5_data_ready_pointer_leak.rar.uu \
test_read_format_rar5_decode_number_out_of_bounds_read.rar.uu \
test_read_format_rar5_different_solid_window_size.rar.uu \
test_read_format_rar5_different_window_size.rar.uu \
test_read_format_rar5_different_winsize_on_merge.rar.uu \
test_read_format_rar5_distance_overflow.rar.uu  \
test_read_format_rar5_extra_field_version.rar.uu  \
test_read_format_rar5_fileattr.rar.uu  \
test_read_format_rar5_hardlink.rar.uu  \
test_read_format_rar5_invalid_dict_reference.rar.uu  \
test_read_format_rar5_leftshift1.rar.uu  \
test_read_format_rar5_leftshift2.rar.uu  \
test_read_format_rar5_multiarchive.part01.rar.uu  \
test_read_format_rar5_multiarchive.part02.rar.uu  \
test_read_format_rar5_multiarchive.part03.rar.uu  \
test_read_format_rar5_multiarchive.part04.rar.uu  \
test_read_format_rar5_multiarchive.part05.rar.uu  \
test_read_format_rar5_multiarchive.part06.rar.uu  \
test_read_format_rar5_multiarchive.part07.rar.uu  \
test_read_format_rar5_multiarchive.part08.rar.uu  \
test_read_format_rar5_multiarchive_solid.part01.rar.uu  \
test_read_format_rar5_multiarchive_solid.part02.rar.uu  \
test_read_format_rar5_multiarchive_solid.part03.rar.uu  \
test_read_format_rar5_multiarchive_solid.part04.rar.uu  \
test_read_format_rar5_multiple_files.rar.uu  \
test_read_format_rar5_multiple_files_solid.rar.uu  \
test_read_format_rar5_nonempty_dir_stream.rar.uu  \
test_read_format_rar5_owner.rar.uu  \
test_read_format_rar5_readtables_overflow.rar.uu  \
test_read_format_rar5_sfx.exe.uu \
test_read_format_rar5_solid.rar.uu  \
test_read_format_rar5_stored.rar.uu  \
test_read_format_rar5_stored_manyfiles.rar.uu  \
test_read_format_rar5_symlink.rar.uu  \
test_read_format_rar5_truncated_huff.rar.uu  \
test_read_format_rar5_win32.rar.uu  \
test_read_format_rar5_window_buf_and_size_desync.rar.uu \
test_read_format_rar_binary_data.rar.uu  \
test_read_format_rar_compress_best.rar.uu  \
test_read_format_rar_compress_normal.rar.uu  \
test_read_format_rar_encryption_data.rar.uu  \
test_read_format_rar_encryption_header.rar.uu  \
test_read_format_rar_encryption_partially.rar.uu  \
test_read_format_rar_invalid1.rar.uu  \
test_read_format_rar_multi_lzss_blocks.rar.uu  \
test_read_format_rar_multivolume.part0001.rar.uu  \
test_read_format_rar_multivolume.part0002.rar.uu  \
test_read_format_rar_multivolume.part0003.rar.uu  \
test_read_format_rar_multivolume.part0004.rar.uu  \
test_read_format_rar_noeof.rar.uu  \
test_read_format_rar_ppmd_lzss_conversion.rar.uu  \
test_read_format_rar_ppmd_use_after_free.rar.uu  \
test_read_format_rar_ppmd_use_after_free2.rar.uu \
test_read_format_rar_sfx.exe.uu  \
test_read_format_rar_subblock.rar.uu  \
test_read_format_rar_unicode.rar.uu  \
test_read_format_rar_windows.rar.uu  \
test_read_format_raw.bufr.uu  \
test_read_format_raw.data.Z.uu  \
test_read_format_raw.data.gz.uu  \
test_read_format_raw.data.uu  \
test_read_format_tar_concatenated.tar.uu  \
test_read_format_tar_empty_filename.tar.uu  \
test_read_format_tar_empty_pax.tar.Z.uu  \
test_read_format_tar_empty_with_gnulabel.tar.uu  \
test_read_format_tar_filename_koi8r.tar.Z.uu  \
test_read_format_ustar_filename_cp866.tar.Z.uu  \
test_read_format_ustar_filename_eucjp.tar.Z.uu  \
test_read_format_ustar_filename_koi8r.tar.Z.uu  \
test_read_format_warc.warc.uu  \
test_read_format_xar_duplicate_filename_node.xar.uu \
test_read_format_zip.zip.uu  \
test_read_format_zip_7075_utf8_paths.zip.uu  \
test_read_format_zip_7z_deflate.zip.uu \
test_read_format_zip_7z_lzma.zip.uu \
test_read_format_zip_bz2_hang.zip.uu  \
test_read_format_zip_bzip2.zipx.uu  \
test_read_format_zip_bzip2_multi.zipx.uu  \
test_read_format_zip_comment_stored_1.zip.uu  \
test_read_format_zip_comment_stored_2.zip.uu  \
test_read_format_zip_encryption_data.zip.uu  \
test_read_format_zip_encryption_header.zip.uu  \
test_read_format_zip_encryption_partially.zip.uu  \
test_read_format_zip_extra_padding.zip.uu  \
test_read_format_zip_filename_cp866.zip.uu  \
test_read_format_zip_filename_cp932.zip.uu  \
test_read_format_zip_filename_koi8r.zip.uu  \
test_read_format_zip_filename_utf8_jp.zip.uu  \
test_read_format_zip_filename_utf8_ru.zip.uu  \
test_read_format_zip_filename_utf8_ru2.zip.uu  \
test_read_format_zip_high_compression.zip.uu  \
test_read_format_zip_jar.jar.uu  \
test_read_format_zip_length_at_end.zip.uu  \
test_read_format_zip_lzma.zipx.uu  \
test_read_format_zip_lzma_alone_leak.zipx.uu  \
test_read_format_zip_lzma_multi.zipx.uu  \
test_read_format_zip_lzma_stream_end.zipx.uu \
test_read_format_zip_mac_metadata.zip.uu  \
test_read_format_zip_malformed1.zip.uu  \
test_read_format_zip_msdos.zip.uu  \
test_read_format_zip_nested.zip.uu  \
test_read_format_zip_nofiletype.zip.uu  \
test_read_format_zip_padded1.zip.uu  \
test_read_format_zip_padded2.zip.uu  \
test_read_format_zip_padded3.zip.uu  \
test_read_format_zip_ppmd8.zipx.uu  \
test_read_format_zip_ppmd8_crash_1.zipx.uu  \
test_read_format_zip_ppmd8_crash_2.zipx.uu  \
test_read_format_zip_ppmd8_multi.zipx.uu  \
test_read_format_zip_sfx.uu  \
test_read_format_zip_symlink.zip.uu  \
test_read_format_zip_traditional_encryption_data.zip.uu  \
test_read_format_zip_ux.zip.uu  \
test_read_format_zip_winzip_aes128.zip.uu  \
test_read_format_zip_winzip_aes256.zip.uu  \
test_read_format_zip_winzip_aes256_large.zip.uu  \
test_read_format_zip_winzip_aes256_stored.zip.uu  \
test_read_format_zip_with_invalid_traditional_eocd.zip.uu  \
test_read_format_zip_xz_multi.zipx.uu  \
test_read_format_zip_zip64a.zip.uu  \
test_read_format_zip_zip64b.zip.uu  \
test_read_format_zip_zstd.zipx.uu \
test_read_format_zip_zstd_multi.zipx.uu \
test_read_large_splitted_rar_aa.uu  \
test_read_large_splitted_rar_ab.uu  \
test_read_large_splitted_rar_ac.uu  \
test_read_large_splitted_rar_ad.uu  \
test_read_large_splitted_rar_ae.uu  \
test_read_splitted_rar_aa.uu  \
test_read_splitted_rar_ab.uu  \
test_read_splitted_rar_ac.uu  \
test_read_splitted_rar_ad.uu  \
test_read_too_many_filters.gz.uu  \
test_splitted_rar_seek_support_aa.uu  \
test_splitted_rar_seek_support_ab.uu  \
test_splitted_rar_seek_support_ac.uu  \
test_write_disk_appledouble.cpio.gz.uu  \
test_write_disk_hfs_compression.tgz.uu  \
test_write_disk_mac_metadata.tar.gz.uu  \
test_write_disk_no_hfs_compression.tgz.uu

.include <bsd.test.mk>

test_main.o test_main.d: list.h

CLEANFILES+=list.h

# XXX: We skip the truncated filter tests because they are broken
# for built-in decompressors. The reason is that the output buffer
# size is 64K, the truncated files are < 64K and the built-in decompressors
# bail out instead of returning incomplete results.
list.h: ${SRCS.h_libarchive} Makefile
	${TOOL_GREP} -h '^DEFINE_TEST(' ${.ALLSRC} | \
	    ${TOOL_GREP} -v _truncated_filter_ | \
	    ${TOOL_GREP} -v test_compat_pax_libarchive_2x > ${.TARGET}

COPTS.test_archive_string_conversion.c+=	${CC_WNO_STRINGOP_TRUNCATION}
COPTS.test_main.c+=	${CC_WNO_STRINGOP_OVERFLOW}
COPTS.test_write_disk_secure.c+=	${CC_WNO_STRINGOP_OVERFLOW}

