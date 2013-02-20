#include <stdio.h>
#include <windows.h>
#include <newdev.h>

int main(int argc, char **argv) {
	LPCTSTR hardware_id;
	LPCTSTR full_inf_path;
	BOOL reboot_required;
	DWORD rc;

	if (argc < 3) {
		printf("error: 1\n");
		return 1;
	}

	hardware_id = argv[1];
	full_inf_path = argv[2];

	if (!UpdateDriverForPlugAndPlayDevices(NULL, hardware_id, full_inf_path,
	                                       INSTALLFLAG_FORCE, &reboot_required)) {
		rc = GetLastError();
		printf("error: %d\n", (int)rc);
		return rc;
	}

	printf("success\n");
	return 0;
}
