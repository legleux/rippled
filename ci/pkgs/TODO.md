1. Combine build.sh scripts
2. Generate changelog from cliff.toml for deb (at least)
3. Modernize rpm.spec file 
   1. Update variables to use variables from build.sh
4. Modernize debian.rules
   1. Add src package
   2. Ensure systemd doesn't restart rippled on install
   3.  
