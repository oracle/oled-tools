Name: oled-tools
Version: 1.0.2
Release: 1%{?dist}
Summary: Diagnostic tools for more efficient and faster debugging on Oracle Linux
# kcore-utils requirements
%ifarch x86_64
BuildRequires: zlib-devel
BuildRequires: bzip2-devel
BuildRequires: elfutils-devel
%endif
Requires: python3
Requires: python3-psutil
Requires: drgn
Requires: drgn-tools
BuildRequires: systemd
BuildRequires: python3-devel
BuildRequires: selinux-policy
BuildRequires: selinux-policy-devel
BuildRequires: selinux-policy-targeted
Requires: selinux-policy
Requires(post): selinux-policy-base
Requires(post): selinux-policy-targeted
Requires(post): policycoreutils
Requires(post): libselinux-utils
Group: Development/Tools
License: GPLv2

%global debug_package %{nil}
%global selinuxtype targeted

Source0: %{name}-%{version}.tar.gz

%description
oled-tools is a collection of command line tools, scripts, config files, etc.,
that will aid in faster and better debugging of problems on Oracle Linux. It
contains: lkce, kstack, memstate, oomwatch, syswatch, scanfs, and vmcore_sz.

%prep
%setup -q

# only required for kcore-utils
%ifarch x86_64
%build
make %{?_smp_mflags}
%endif


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT DIST=%{?dist} SPECFILE="1"

%post
restorecon -vF /var/lib/pcp/config/pmieconf/oled ||:
restorecon -vF /var/lib/pcp/config/pmieconf/oled/oomwatch ||:
semodule -X200 -s %{selinuxtype} -i /usr/share/selinux/packages/targeted/pcp-oomwatch.pp.bz2

systemctl restart pmie || :

if [ $1 -ge 1 ] ; then
	# package upgrade
	if [ ! `grep -q '^kdump_pre /etc/oled/kdump_pre.sh$' /etc/kdump.conf 2> /dev/null` ]; then #present
		sed --in-place '/kdump_pre \/etc\/oled\/kdump_pre.sh/d' /etc/kdump.conf 2> dev/null ||:
		sed --in-place '/extra_bins \/bin\/timeout/d' /etc/kdump.conf 2> /dev/null || :
	fi

	if [ ! `grep -q '^extra_bins /bin/timeout$' /etc/kdump.conf 2> /dev/null` ]; then #present
		sed --in-place '/extra_bins \/bin\/timeout/d' /etc/kdump.conf 2> /dev/null || :
	fi

	if [ ! `grep -q '^kdump_pre /etc/oled/lkce/kdump_pre.sh$' /etc/kdump.conf 2> /dev/null` ]; then
		# force regeneration of the lkce kdump script
		oled lkce disable_kexec > /dev/null || :
		oled lkce enable_kexec > /dev/null || :
	fi
	oled oomwatch --reload &>/dev/null || :
fi

# configure lkce
[ -f /etc/oled/lkce/lkce.conf ] || oled lkce configure --default > /dev/null

%preun
if [ $1 -lt 1 ] ; then
	# package uninstall, not upgrade
	oled lkce disable_kexec > /dev/null || :
	oled oomwatch -d &>/dev/null ||:
	systemctl restart pmie &>/dev/null ||:
fi

%postun
if [ $1 -eq 0 ]; then
	semodule -n -X200 -r pcp-oomwatch &>/dev/null || :
fi

%clean
rm -rf $RPM_BUILD_ROOT

%pretrans -p <lua>
-- Per https://docs.fedoraproject.org/en-US/packaging-guidelines/Directory_Replacement/
path = "/usr/libexec/oled-tools/scripts"
st = posix.stat(path)
if st and st.type == "directory" then
  status = os.rename(path, path .. ".rpmmoved")
  if not status then
    suffix = 0
    while not status do
      suffix = suffix + 1
      status = os.rename(path .. ".rpmmoved", path .. ".rpmmoved." .. suffix)
    end
    os.rename(path, path .. ".rpmmoved")
  end
end

%files
%{_unitdir}/oled-tools-scripts.service
%defattr(-,root,root,-)

%license LICENSE.txt
%doc README.md
%config(noreplace) /etc/oled/oomwatch.json

%{_sbindir}/oled
%{_mandir}/man8/*

# owned directories

# memstate_lib python module
%{python3_sitelib}/memstate_lib/
/var/lib/pcp/config/pmieconf/oled/

# Files for oomwatch configuration
/var/lib/pcp/config/pmieconf/oled/oomwatch
/usr/share/selinux/packages/targeted/pcp-oomwatch.pp.bz2
/etc/sudoers.d/99-pcp-oled-oomwatch

# all oled-tools configuration
/etc/oled/

# track auto generated files so that they are removed during uninstall
%ghost /etc/oled/lkce/crash_cmds_file
%ghost /etc/oled/lkce/lkce.conf
%ghost /usr/libexec/oled-tools/scripts.rpmmoved

# all oled-tools subcommands and scripts
%{_libexecdir}/oled-tools/

%changelog
* Tue Jan 07 2025 Ryan McCabe <ryan.m.mccabe@oracle.com> - 1.0.2-1
- Update to v1.0.2
- Add the oled oomwatch command.
  (Sharad Raykhere, Ryan McCabe, Jeffery Yoder, Sagar Sagar)
  [Orabug: 37453543]

* Tue Nov 19 2024 Ryan McCabe <ryan.m.mccabe@oracle.com> - 1.0.1-1
* Update to v1.0.1
- Update LKCE to work on OL7 or later (Ryan McCabe)
- Improvements and cleanups for LKCE (Ryan McCabe, Jeffery Yoder)
  [Orabug: 36669742]
- Add the 'scripts' oled sub-command (Jose Lombera)
- Update the sense of idle% in the syswatch sub-command (Ryan McCabe)
  [Orabug: 36622809]
- Add new dtrace scripts (Manjunath Patil, Shminderjit Singh)
  [Orabug: 36653828]
- Improvements to existing dtrace scripts
  (Shminderjit Singh, Nagappan Ramasamy Palaniappan, Manjunath Patil)
  [Orabugs: 36914572, 36914572, 36572024]
- Add minimum and maximum compatible kernel versions for dtrace scripts
  (Nagappan Ramasamy Palaniappan, Sharad Raykhere)
- Improvements to the memstate sub-command (Aruna Ramakrishna, Jianfeng Wang)
  [Orabugs: 36432022, 36432139, 36432089, 36569938, 36432017, 36432149]
- Removed the filecache and dentrycache sub-commands (Jose Lombera)
  [Orabug: 36274217]
- Removed the -I and -s options from the kstack sub-command (Jose Lombera)
  [Orabug: 36268453]

* Thu Nov 9 2023 Jose Lombera <jose.lombera@oracle.com> - 0.7-1
- Update to v0.7.
- Clean up oled-tools.spec
- Add doc and license files to RPM
- Migrate all Python scripts to Python3
- Install oled subcommands in /usr/libexec/
- Clean up python scripts and fix pylint/flake8/mypy/bandit errors/warnings
- oled: escape passthrough arguments (Jose Lombera) [Orabug: 35064194]
- kcore-utils: fix build in OL9 (Jose Lombera)
- Only build kcore-utils on x86-64
- syswatch: support monitoring all CPU stat metrics
- memsate: several improvements
- memstate: NUMA: fix per-node memory accounting
- memstate: determine PAGE_SIZE at runtime (Aruna Ramakrishna)
  [Orabug: 35074520]
- Add tool scanfs (Srikanth C S) [Orabug: 34502391]
- Add tool vmcore_sz (Partha Satapathy, Srikanth C S) [Orabug: 35824470]

* Sun Jul 2 2023 Jose Lombera <jose.lombera@oracle.com> - 0.6-2
- Release oled-tools-0.6-2
- Reapply missing fixes from v0.5-5 [Orabugs: 32038044, 33104580, 33107277,
  33271828, 33304018]
- Change license back to GPLv2.
- Add pragma to d scripts (Nagappan Ramasamy Palaniappan) [Orabug: 34855326]
- Fix bugs in lkce (Manjunath Patil) [Orabug: 35097936]

* Mon Jan 30 2023 Manjunath Patil <manjunath.b.patil@oracle.com> - 0.6-1
- Note: this release was removed from yum
- release oled-tools-0.6-1
- Remove tools memtracker, smtool and topstack
- Remove lkce's command 'kdump_report' (Srinivas Eeda)
- Add diagnostic DTrace scripts (Manjunath Patil, Praveen Kumar Kannoju,
  Rama Nichanamatlu)
- Add tool syswatch (Jose Lombera) [Orabug: 34858875]
- Reinstate dependencies

* Fri Sep 17 2021 Aruna Ramakrishna <aruna.ramakrishna@oracle.com> - 0.5-5
- Release oled-tools-0.5-5

* Thu Sep 02 2021 Mridula Shastry <mridula.c.shastry@oracle.com>
- Smtool: Avoid failure when mitigation for a particular variant is
 not available [Orabug: 33287128]
- Smtool: Avoid redundant messages during verbose scan [Orabug: 33309752]
- Smtool: Add support for detection/mitigation for SRBDS [Orabug: 33032240]
- Smtool: Fix scanning of commandline parameters for TSX_Async_Abort [Orabug: 33043269]
- Smtool: Fix verbose scan [Orabug: 33044339]
- Smtool: Minor bug fixes [Orabug: 32999593]
- Smtool: Fix enable/disable mitigations on Oracle VM Servers [Orabug: 33005737]
- Smtool: Fix enabling/disabling mitigations for TSX Async Abort [Orabug: 33005751]
- Smtool: Misc bug fixes [Orabug: 32920986]

* Mon May 31 2021 Manjunath Patil <manjunath.b.patil@oracle.com>
- Rewrite lkce

* Wed May 19 2021 Aruna Ramakrishna <aruna.ramakrishna@oracle.com>
- Add man pages for filecache and dentrycache

* Thu Apr 8 2021 Cesar Roque <cesar.roque@oracle.com>
- Integrate topstack into oled-tools [Orabug: 32734650]
- Integrate kstack into oled-tools [Orabug: 32545451]

* Thu Apr 1 2021 Aruna Ramakrishna <aruna.ramakrishna@oracle.com>
- Fix kcore-utils Jenkins build issues
- Remove gather from oled-tools [Orabug: 32461332]
- Integrate memtracker into oled-tools [Orabug: 32430673]
- Integrate memstate into oled-tools [Orabug: 32425345]
- Add filecache and dentrycache to oled-tools rpm build.

* Tue Jan 5 2021 Mridula Shastry <mridula.c.shastry@oracle.com>
- Enable smtool on OL8. [Orabug: 30441144]
- Enabled kdump-utils and lkce to run on OL8. [Orabug: 32299961]

* Fri Sep 4 2020 Manjunath Patil <manjunath.b.patil@oracle.com>
- Remove release string from oled version [Orabug: 31793813]

* Mon Aug 17 2020 Manjunath Patil <manjunath.b.patil@oracle.com> - 0.1-4
- Release oled-tools-0.1-4

* Fri Jul 17 2020 Manjunath Patil <manjunath.b.patil@oracle.com>
- Re-organize Makefile and spec files

* Sun May 10 2020 Manjunath Patil <manjunath.b.patil@oracle.com> - 0.1
- First version
