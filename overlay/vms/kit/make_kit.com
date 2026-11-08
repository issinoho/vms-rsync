$! MAKE_KIT.COM - build the PCSI kit for this node's architecture
$!
$! Usage:  @[.VMS.KIT]MAKE_KIT
$! Needs a built [.VMS]RSYNC.EXE (BUILD.COM).  Writes the kit to [.KIT_<arch>].
$! The product description, text module and documentation were generated
$! by tools/prepare.sh into [.VMS.KIT].
$!
$ set noon
$ status = 44
$ saved_default = f$environment("DEFAULT")
$ proc = f$environment("PROCEDURE")
$ kitdir = f$parse(proc,,,"DEVICE") + f$parse(proc,,,"DIRECTORY")
$ set default 'kitdir'
$ set default [--]
$ top = f$environment("DEFAULT")
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "I64VMS"
$ if arch .eqs. "X86_64" then base = "X86VMS"
$ if arch .eqs. "ALPHA" then base = "AXPVMS"
$!
$! KIT_PRODUCER, PCSI_VERSION, KIT_VERSION from kit.env
$ open/read env [.VMS.KIT]KIT.ENV
$env_loop:
$ read/end=env_done env line
$ name = f$element(0, "=", line)
$ 'name' = f$element(1, "=", line)
$ goto env_loop
$env_done:
$ close env
$!
$ image = "[.VMS]RSYNC.EXE"
$ if f$search(image) .eqs. ""
$ then
$   write sys$error "MAKE_KIT: no ''image'; build first"
$   goto done
$ endif
$!
$! Gather the files flat in [.KIT_<arch>.MAT]: PRODUCT PACKAGE looks each
$! one up by name in the material directory (destinations are in the PDF).
$ mat = "[.KIT_''arch'.MAT]"
$ out = "[.KIT_''arch']"
$ if f$search("KIT_''arch'.DIR") .eqs. "" then create/directory 'out'
$ if f$search("[.KIT_''arch']MAT.DIR") .eqs. "" then create/directory 'mat'
$ if f$search("''out'*.PCSI;*") .nes. "" then delete/nolog 'out'*.PCSI;*
$ if f$search("''mat'*.*;*") .nes. "" then delete/nolog 'mat'*.*;*
$ copy/nolog 'image' 'mat'RSYNC.EXE
$ copy/nolog [.VMS.KIT]RSYNC$STARTUP.COM,RSYNC$SETUP.COM,README.VMS 'mat'
$ copy/nolog [.VMS.KIT.DOC]*.* 'mat'
$ matspec = f$parse(mat,,,"DEVICE","NO_CONCEAL") + f$parse(mat,,,"DIRECTORY","NO_CONCEAL")
$!
$ write sys$output "MAKE_KIT: ''KIT_PRODUCER' ''base' RSYNC ''PCSI_VERSION' (''KIT_VERSION')"
$ product package RSYNC -
    /producer='KIT_PRODUCER' /base_system='base' /version='PCSI_VERSION' -
    /source=[.VMS.KIT]RSYNC-'base'.PCSI$DESC -
    /material='matspec' -
    /destination='out' -
    /format=sequential -
    /options=noconfirm /log
$ status = $status
$ kit = f$search("''out'*.PCSI")
$ if kit .nes. "" then write sys$output "MAKE_KIT: kit ", kit
$ if kit .eqs. "" then status = 44
$done:
$ set default 'saved_default'
$ exit status
