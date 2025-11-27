@Echo OFF
Setlocal EnableDelayedExpansion

Set LogFileFolderPath=%APPDATA%\OpenAL\

If not "%~1"=="" (
	Set ALSOFT_LOGFILE=alsoft_error.txt
	Set ALSOFT_LOGLEVEL=3
	Set DSOAL_LOGFILE=dsoal_error.txt
	Set DSOAL_LOGLEVEL=4
	For %%A in (%1) do (
		CD "%%~dpA"
		PushD "%%~dpA"
		"%%~nxA"
		)
	IF EXIST "!DSOAL_LOGFILE!" (
		Set LogExists=True
		Echo !DSOAL_LOGFILE! has been created.
		)
	IF EXIST "!ALSOFT_LOGFILE!" (
		Set LogExists=True
		Echo !ALSOFT_LOGFILE! has been created.
		)
	IF DEFINED LogExists (
		Echo Press any key to open log file/s in your text editor or close this window.
		pause >NUL
		IF EXIST "!DSOAL_LOGFILE!"  (START /B /SEPARATE "" "!DSOAL_LOGFILE!"  >NUL)
		IF EXIST "!ALSOFT_LOGFILE!" (START /B /SEPARATE "" "!ALSOFT_LOGFILE!" >NUL)
		) else (
		Echo Log file/s were not created.
		Echo This can be caused by the executable not having permissions to create files in its folder or being run as administrator
		Echo So instead of dropping an executable, you can try running this script directly to set environmental variables
		Echo Press any key to close this window.
		pause>Nul
		exit
		)
	) else (
	NET SESSION >nul 2>&1
	IF !ERRORLEVEL! EQU 0 (
		REG delete HKCU\Environment /F /V DSOAL_LOGLEVEL 1>>NUL 2>&1
		REG delete HKCU\Environment /F /V DSOAL_LOGFILE 1>>NUL 2>&1
		REG delete HKCU\Environment /F /V ALSOFT_LOGLEVEL 1>>NUL 2>&1
		REG delete HKCU\Environment /F /V ALSOFT_LOGFILE 1>>NUL 2>&1
		REG delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /F /V DSOAL_LOGLEVEL 1>>NUL 2>&1
		REG delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /F /V DSOAL_LOGFILE 1>>NUL 2>&1
		REG delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /F /V ALSOFT_LOGLEVEL 1>>NUL 2>&1
		REG delete "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /F /V ALSOFT_LOGFILE 1>>NUL 2>&1

		Echo SYSTEM environment variables have now been removed so logging is now disabled.
		Echo You can close this window or proceed to ^(re^)enable.
		Echo.
		Echo *This is ONLY meant for executables that are NOT run as administrator but ARE located in protected folders
		Echo like Program files\*\, which require administrator rights to create/modify ^(log^) files*
		Echo The log files will be saved into "!LogFileFolderPath!".
		Echo - If the executable ISN'T run as administrator and ISN'T in a protected folder,
		Echo   then close this window and drop the executable directly into the script.
		Echo - If the executable IS run as administrator,
		Echo   then close this window and run the script NOT as an administrator to use USER environment variables.
		Echo Press any key to enable DSOAL and OpenAL Soft logging via SYSTEM environment variables.
		Pause>Nul
		Echo Setting SYSTEM environment variables to enable logging...
		If not exist "!LogFileFolderPath!" (MkDir "!LogFileFolderPath!")
		setX DSOAL_LOGLEVEL "4" /M 1>>NUL 2>&1
		setX DSOAL_LOGFILE "!LogFileFolderPath!dsoal_error.txt" /M 1>>NUL 2>&1
		setX ALSOFT_LOGLEVEL "3" 1>>NUL /M 2>&1
		setX ALSOFT_LOGFILE "!LogFileFolderPath!alsoft_error.txt" /M 1>>NUL 2>&1
		Echo.
		Echo SYSTEM environment variables set
		Echo - If you still don't see the log files, the game might not be using DirectSound or OpenAL,
		Echo - Keep in mind that log files can reach GBs in size and affect performance,
		Echo   so to disable logging, just run the script again without pressing a key that sets environment variables
		Echo.
		Echo Press any key to open the location where log files will be saved: ^(!LogFileFolderPath!^)
		Pause>Nul
		explorer.exe "!LogFileFolderPath!"
		Exit
		) else (
		REG delete HKCU\Environment /F /V DSOAL_LOGLEVEL 1>>NUL 2>&1
		REG delete HKCU\Environment /F /V DSOAL_LOGFILE 1>>NUL 2>&1
		REG delete HKCU\Environment /F /V ALSOFT_LOGLEVEL 1>>NUL 2>&1
		REG delete HKCU\Environment /F /V ALSOFT_LOGFILE 1>>NUL 2>&1
		Echo USER environment variables have now been removed so logging is now disabled.
		Echo You can close this window or proceed to ^(re^)enable.
		Echo.
		Echo *This is ONLY meant for executables that ARE run as administrator AND/OR are located in protected folders
		Echo like Program files\*\, which require administrator rights to create/modify ^(log^) files*
		Echo The log files will be created in the same folder as the executable.
		Echo - If the executable ISN'T run as administrator and ISN'T in a protected folder,
		Echo   then close this window and drop the executable directly into the script.
		Echo - If the executable ISNT run as administrator but IS in a protected folder,
		Echo   then close this window and run the SCRIPT as an administrator to use SYSTEM environment variables.
		Echo Press any key to enable DSOAL and OpenAL Soft logging via USER environment variables.
		Pause>Nul
		Echo Setting USER environment variables to enable logging...
		setX DSOAL_LOGLEVEL "4" 1>>NUL 2>&1
		setX DSOAL_LOGFILE "dsoal_error.txt" 1>>NUL 2>&1
		setX ALSOFT_LOGLEVEL "3" 1>>NUL 2>&1
		setX ALSOFT_LOGFILE "alsoft_error.txt" 1>>NUL 2>&1
		Echo.
		Echo USER environment variables set
		Echo - If you still don't see the log files, the game might not be using DirectSound or OpenAL,
		Echo - Keep in mind that log files can reach GBs in size and affect performance,
		Echo   so to disable logging, just run the script again without pressing a key that sets environment variables
		Pause>Nul
		)
	)
Exit