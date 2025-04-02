#include <windows.h>
#include <iostream>
#include <tchar.h>
#include <setupapi.h>
#include <devguid.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <string>
#include <winioctl.h>
#include <vector>
#include <initguid.h> 
#include <guiddef.h> 
#include <algorithm>
#include <shellapi.h>
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "dxgi.lib")
using namespace std;
using Microsoft::WRL::ComPtr;

string TrimString(const string& str) {
    auto start = find_if(str.begin(), str.end(), [](unsigned char ch) { return !isspace(ch); });
    auto end = find_if(str.rbegin(), str.rend(), [](unsigned char ch) { return !isspace(ch); }).base();
    return (start < end ? string(start, end) : "");
}

const GUID PARTITION_BASIC_DATA_GUID =
{ 0xebd0a0a2, 0xb9e5, 0x4433, { 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7 } };

void RunAsAdmin() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(SHELLEXECUTEINFOW) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        MessageBox(NULL, L"Не удалось запустить с правами администратора!", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}
bool IsAdmin() {
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID pAdminGroup;

    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminGroup)){
        CheckTokenMembership(NULL, pAdminGroup, &isAdmin);
        FreeSid(pAdminGroup);
    }
    return isAdmin;
}
bool IsSSD(HANDLE hDevice) {
    STORAGE_PROPERTY_QUERY query = { StorageDeviceTrimProperty, PropertyStandardQuery };
    DEVICE_TRIM_DESCRIPTOR trimDescriptor = { 0 };
    DWORD bytesReturned;

    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
        &trimDescriptor, sizeof(trimDescriptor), &bytesReturned, NULL)) {
        return trimDescriptor.TrimEnabled;
    }
    return false;
}

void GetCPUInfo() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    HKEY hKey;
    LONG result;
    TCHAR value[256];
    DWORD size = sizeof(value);
    string arch;
    switch (sysInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64"; break;
    case PROCESSOR_ARCHITECTURE_ARM: arch = "ARM"; break;
    case PROCESSOR_ARCHITECTURE_ARM64: arch = "ARM64"; break;
    case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
    default: arch = "Unknown";
    }
    // |\/\/\/| //
    result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        if (RegQueryValueEx(hKey, L"ProcessorNameString", NULL, NULL, (LPBYTE)value, &size) == ERROR_SUCCESS) {
            wcout << L"Процессор: " << value << endl;
        }
        RegCloseKey(hKey);
    }
    // |/\/\/\| //
    cout << "Архитектура: " << arch << endl;
    cout << "Количество Ядер : " << sysInfo.dwNumberOfProcessors << endl;
}
void GetMemoryInfo() {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);
    cout << "RAM: " << memInfo.ullTotalPhys / (1024 * 1024) << " MB" << endl;
    cout << "Свободная RAM: " << memInfo.ullAvailPhys / (1024 * 1024) << " MB" << endl;
    cout << "Используется: " << memInfo.dwMemoryLoad << " %" << endl;
}

void GetLogicalDrivesInfo() {
    cout << "Логические диски:\n\n";
    DWORD drives = GetLogicalDrives();

    for (char letter = 'A'; letter <= 'Z'; letter++) {
        if (drives & (1 << (letter - 'A'))) {
            wstring drive = wstring(1, letter) + L":\\";
            ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;

            if (GetDiskFreeSpaceEx(drive.c_str(), &freeBytes, &totalBytes, &totalFreeBytes)) {
                cout << "Диск: " << string(drive.begin(), drive.end()) << " "
                    << (totalBytes.QuadPart / (1024 * 1024 * 1024)) << " GB, Доступно: "
                    << (freeBytes.QuadPart / (1024 * 1024 * 1024)) << " GB\n";
            }

            WCHAR fileSystemName[MAX_PATH] = { 0 };
            if (GetVolumeInformationW(drive.c_str(), NULL, 0, NULL, NULL, NULL, fileSystemName, MAX_PATH)) {
                wcout << L"  Файловая система: " << fileSystemName << endl;
            }

            wstring volumePath = L"\\\\.\\" + drive.substr(0, 2);
            HANDLE hVolume = CreateFileW(volumePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL, OPEN_EXISTING, 0, NULL);

            if (hVolume != INVALID_HANDLE_VALUE) {
                VOLUME_DISK_EXTENTS diskExtents;
                DWORD bytesReturned;
                if (DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
                    &diskExtents, sizeof(diskExtents), &bytesReturned, NULL)) {
                    cout << "  Относится к Диск " << diskExtents.Extents[0].DiskNumber << endl;
                    cout << "----------------------------------------\n";
                }
                CloseHandle(hVolume);
            }
        }
    }
}
DWORD GetPartitionCount(HANDLE hDevice) {
    DWORD bytesReturned;
    DWORD size = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 16 * sizeof(PARTITION_INFORMATION_EX);
    BYTE* buffer = new BYTE[size];
    DRIVE_LAYOUT_INFORMATION_EX* layout = (DRIVE_LAYOUT_INFORMATION_EX*)buffer;

    if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, buffer, size, &bytesReturned, NULL)) {
        DWORD count = 0;
        for (DWORD i = 0; i < layout->PartitionCount; i++) {
            const PARTITION_INFORMATION_EX& partition = layout->PartitionEntry[i];

            if (layout->PartitionStyle == PARTITION_STYLE_MBR) {

                if (partition.Mbr.PartitionType != PARTITION_ENTRY_UNUSED &&
                    partition.Mbr.PartitionType != 0xEE) {
                    count++;
                    //cout << "  MBR Partition Type: " << hex << (int)partition.Mbr.PartitionType << dec << endl;
                }
            }
            else if (layout->PartitionStyle == PARTITION_STYLE_GPT) {

                if (partition.Gpt.PartitionType != PARTITION_BASIC_DATA_GUID) {
                    count++;
                }
            }
        }
        delete[] buffer;
        return count;
    }
    delete[] buffer;
    return 0;
}
void GetPhysicalDrivesInfo() {
    cout << "Физические Диски:\n\n";

    for (int i = 0; i < 10; i++) {
        string drivePath = "\\\\.\\PhysicalDrive" + to_string(i);
        HANDLE hDevice = CreateFileA(drivePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);

        if (hDevice == INVALID_HANDLE_VALUE) continue;

        STORAGE_PROPERTY_QUERY query = { StorageDeviceProperty, PropertyStandardQuery };
        STORAGE_DESCRIPTOR_HEADER header = { 0 };
        DWORD bytesReturned;

        if (!DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
            &header, sizeof(header), &bytesReturned, NULL)) {
            CloseHandle(hDevice);
            continue;
        }

        vector<BYTE> buffer(header.Size);
        if (!DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
            buffer.data(), header.Size, &bytesReturned, NULL)) {
            CloseHandle(hDevice);
            continue;
        }

        STORAGE_DEVICE_DESCRIPTOR* descriptor = (STORAGE_DEVICE_DESCRIPTOR*)buffer.data();

        string model = descriptor->ProductIdOffset ? TrimString((char*)buffer.data() + descriptor->ProductIdOffset) : "Unknown";
        string serial = descriptor->SerialNumberOffset ? TrimString((char*)buffer.data() + descriptor->SerialNumberOffset) : "Unknown";
        string vendor = descriptor->VendorIdOffset ? TrimString((char*)buffer.data() + descriptor->VendorIdOffset) : "Unknown";

        string busType = "Unknown";
        switch (descriptor->BusType) {
        case BusTypeSata: busType = "SATA"; break;
        case BusTypeUsb:  busType = "USB"; break;
        case BusTypeNvme: busType = "NVMe"; break;
        }

        bool isSSD = IsSSD(hDevice);

        if (vendor == "Unknown") {
            cout << "Диск " << i << ":" <<
                "\n  Модель: " << model <<
                "\n  Тип: " << (isSSD ? "SSD" : "HDD") <<
                "\n  Серийный номер: " << serial <<
                "\n  Интерфейс: " << busType << endl;
        }else {
            cout << "Диск " << i << ":" <<
                "\n  Производитель: " << vendor <<
                "\n  Модель: " << model <<
                "\n  Тип: " << (isSSD ? "SSD" : "HDD") <<
                "\n  Серийный номер: " << serial <<
                "\n  Интерфейс: " << busType << endl;
        }


        DISK_GEOMETRY_EX diskGeometry;
        if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &diskGeometry, sizeof(diskGeometry),
            &bytesReturned, NULL)) {
            ULONGLONG diskSize = diskGeometry.DiskSize.QuadPart / (1024 * 1024 * 1024);
            cout << "  Размер: " << diskSize << " GB" << endl;
        }

        DWORD partitions = GetPartitionCount(hDevice);
        cout << "  Разделов: " << partitions << endl;

        CloseHandle(hDevice);
        cout << "----------------------------------------\n";
    }
}

void GetGPUInfo() {
    ComPtr<IDXGIFactory1> pFactory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory);

    ComPtr<IDXGIAdapter1> pAdapter;
    int index = 0;
    while (pFactory->EnumAdapters1(index, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);
        wstring ws(desc.Description);
        string gpuName(ws.begin(), ws.end());
        int vramGB = static_cast<int>(round(static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0)));
        int sharedGB = static_cast<int>(round(static_cast<double>(desc.SharedSystemMemory) / (1024.0 * 1024.0 * 1024.0)));
        int totalGB = vramGB + sharedGB;
        if (totalGB == sharedGB) {
            cout << "Видеокарта: " << gpuName << endl;
            cout << "Общая память: " << sharedGB << " GB" << endl;
            cout << "Суммарная доступная память: " << totalGB << " GB" << endl;
        }
        else {
            cout << "Видеокарта: " << gpuName << endl;
            cout << "Выделенная VRAM: " << vramGB << " GB" << endl;
            cout << "Общая память: " << sharedGB << " GB" << endl;
            cout << "Суммарная доступная память: " << totalGB << " GB" << endl;
        }
        index--;
        pAdapter.Reset();
    }
}
void GetUSBDevices() {
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_USB, NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    cout << "USB Устройства: " << endl;
    for (DWORD i = 0;SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData);i++) {
        TCHAR deviceName[256];
        DWORD size = 0;
        if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)deviceName, sizeof(deviceName), &size)) {
            wcout << L"- " << deviceName << endl;
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
}
void GetMotherboardInfo() {
    // |\/\/\/| //
    HKEY hKey;
    TCHAR manufacturer[256], product[256];
    DWORD size = sizeof(manufacturer);

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueEx(hKey, L"BaseBoardManufacturer", NULL, NULL, (LPBYTE)manufacturer, &size);
        size = sizeof(product);
        RegQueryValueEx(hKey, L"BaseBoardProduct", NULL, NULL, (LPBYTE)product, &size);
        RegCloseKey(hKey);
    }
    // |/\/\/\| //
    wcout << L"Материнская плата: \n" << endl;
    wcout << L"Производитель: " << manufacturer << endl;
    wcout << L"Модель: " << product << endl;
}

int main()
{
    if(!IsAdmin()){
        RunAsAdmin();
        return 0;
    }
    setlocale(LC_ALL, "rus");
    cout << "_____________________________________________\n" << endl;
    GetCPUInfo();
    cout << "_____________________________________________\n" << endl;
    GetMemoryInfo();
    cout << "_____________________________________________\n" << endl;
    GetPhysicalDrivesInfo();
    cout << "_____________________________________________\n" << endl;
    GetLogicalDrivesInfo();
    cout << "_____________________________________________\n" << endl;
    GetGPUInfo();
    cout << "_____________________________________________\n" << endl;
    GetUSBDevices();
    cout << "_____________________________________________\n" << endl;
    GetMotherboardInfo();
    cout << "_____________________________________________\n" << endl;
    system("pause");
    return 0;
}