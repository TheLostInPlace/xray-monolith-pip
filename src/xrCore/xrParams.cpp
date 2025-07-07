#include "stdafx.h"
#include "xrParams.h"

#include <magic_enum/magic_enum.hpp>

template <>
struct magic_enum::customize::enum_range<ECoreParams> 
{
	static constexpr bool is_flags = true;
	static constexpr int min = (const int)ECoreParams::ECOREPARAMSMIN;
	static constexpr int max = (const int)ECoreParams::size-1;
};

void xrParams::LoadParams()
{
	xr_string CommandLine = Core.Params;
	auto CommandList = CommandLine.Split(' ');

	for (xr_string Command : CommandList)
	{
		if (!Command.StartWith("-"))
			continue;

		Command = Command.substr(1);

		if (auto EnumData = magic_enum::enum_cast<ECoreParams>(Command))
		{
			Core.ParamsData.set(EnumData.value(), true);
		}
	}
}