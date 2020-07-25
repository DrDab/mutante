#include <ntifs.h>
#include "log.h"
#include "utils.h"
#include "shared.h"
#include "smbios.h"

char* GetString(SMBIOS_HEADER* header, SMBIOS_STRING string)
{
	const auto* start = reinterpret_cast<const char*>(header) + header->Length;

	if (!string || *start == 0)
		return nullptr;

	while (--string)
	{
		start += strlen(start) + 1;
	}

	return const_cast<char*>(start);
}

NTSTATUS ProcessTable(SMBIOS_HEADER* header)
{
	if (!header->Length)
		return STATUS_UNSUCCESSFUL;

	if (header->Type == 0)
	{
		auto* type0 = reinterpret_cast<SMBIOS_TYPE0*>(header);
		Log::Print("test: %u\n", type0->BiosVersion);
		Log::Print("test: %u\n", type0->BiosSize);

		auto* text = GetString(header, type0->Vendor);
		Log::Print("Vendor: %s\n", text);
		auto* text2 = GetString(header, type0->BiosVersion);
		Log::Print("Version: %s\n", text2);
	}

	return STATUS_SUCCESS;
}

NTSTATUS LoopTables(void* mapped)
{
	const int size = 256;

	auto* endAddress = static_cast<char*>(mapped) + size;
	while (true)
	{
		auto* header = static_cast<SMBIOS_HEADER*>(mapped);
		if (header->Type == 127 && header->Length == 4)
			break;
		
		ProcessTable(header);
		auto* end = static_cast<char*>(mapped) + header->Length;
		while (0 != (*end | *(end + 1))) end++;
		end += 2;
		if (end >= endAddress)
			break;	

		mapped = end;
	}

	MmUnmapIoSpace(mapped, size);
	
	return STATUS_SUCCESS;
}

NTSTATUS Smbios::ChangeSmbiosSerials()
{
	auto* base = Utils::GetModuleBase("ntoskrnl.exe");
	if (!base)
	{
		Log::Print("Failed to find ntoskrnl.sys base!\n");
		return STATUS_UNSUCCESSFUL;
	}

	auto* physicalAddress = static_cast<PPHYSICAL_ADDRESS>(Utils::FindPatternImage(base, "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x74\x00\x8B\x15", "xxx????xxxx?xx")); // WmipFindSMBiosStructure -> WmipSMBiosTablePhysicalAddress
	if (!physicalAddress)
	{
		Log::Print("Failed to find SMBIOS physical address!\n");
		return STATUS_UNSUCCESSFUL;
	}

	physicalAddress = reinterpret_cast<PPHYSICAL_ADDRESS>(reinterpret_cast<char*>(physicalAddress) + 7 + *reinterpret_cast<int*>(reinterpret_cast<char*>(physicalAddress) + 3));
	if (!physicalAddress)
	{
		Log::Print("Physical address is null!\n");
		return STATUS_UNSUCCESSFUL;
	}

	auto* sizeScan = Utils::FindPatternImage(base, "\x8B\x1D\x00\x00\x00\x00\x48\x8B\xD0\x44\x8B\xC3\x48\x8B\xCD\xE8\x00\x00\x00\x00\x8B\xD3\x48\x8B", "xx????xxxxxxxxxx????xxxx");  // WmipFindSMBiosStructure -> WmipSMBiosTableLength
	if (!sizeScan)
	{
		Log::Print("Failed to find SMBIOS size!\n");
		return STATUS_UNSUCCESSFUL;
	}

	const auto size = *reinterpret_cast<ULONG*>(static_cast<char*>(sizeScan) + 6 + *reinterpret_cast<int*>(static_cast<char*>(sizeScan) + 2));
	if (!size)
	{
		Log::Print("SMBIOS size is null!\n");
		return STATUS_UNSUCCESSFUL;
	}

	auto* mapped = MmMapIoSpace(*physicalAddress, size, MmNonCached);
	if (!mapped)
	{
		Log::Print("Failed to map SMBIOS structures!\n");
		return STATUS_UNSUCCESSFUL;
	}
	
	LoopTables(mapped);
	
	MmUnmapIoSpace(mapped, size);
	
	return STATUS_SUCCESS;
}