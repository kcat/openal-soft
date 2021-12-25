/*

EAX OpenAL Extension

Copyright (c) 2020-2021 Boris I. Bendovsky (bibendovsky@hotmail.com) and Contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

*/


#include <exception>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "eax_exception.h"

#include "eax_patch.h"
#include "eax_patch_collection.h"


class AppPatcherException :
	public eax::Exception
{
public:
	explicit AppPatcherException(
		const char* message)
		:
		Exception{"APP_PATCHER", message}
	{
	}
}; // AppPatcherException


class AppPatcherCancelledException :
	public eax::Exception
{
public:
	AppPatcherCancelledException()
		:
		Exception{"APP_PATCHER", "Cancelled."}
	{
	}
}; // AppPatcherCancelledException


static const auto press_enter_to_exit_message = "Press ENTER to exit.\n";


struct PatchStatusDef
{
	using ActionMethod = void (eax::FilePatcher::*)();


	std::string name{};
	std::string answer{};
	std::string action{};
	ActionMethod action_method{};
}; // PatchStatusDef


const PatchStatusDef patch_status_defs[] =
{
	PatchStatusDef
	{
		"Unpatched",
		"p",
		"patch",
		&eax::FilePatcher::apply
	},

	PatchStatusDef
	{
		"Patched",
		"u",
		"unpatch",
		&eax::FilePatcher::revert
	},
};

const PatchStatusDef& get_patch_status_def(
	eax::PatchStatus patch_status)
{
	switch (patch_status)
	{
		case eax::PatchStatus::unpatched:
			return patch_status_defs[0];

		case eax::PatchStatus::patched:
			return patch_status_defs[1];

		default:
			throw AppPatcherException{"Unsupported patch status."};
	}
}


std::string get_input_string(
	const char* message = nullptr)
{
	if (message)
	{
		std::cout << message;
	}

	auto input_string = std::string{};
	std::getline(std::cin, input_string);

	return input_string;
}


int main(
	int argc,
	char* argv[])
try
{
	std::ignore = argc;
	std::ignore = argv;

	std::cout << "=======================================" << std::endl;
	std::cout << "EAX Application Patcher v" << EAX_APP_PATCHER_VERSION << std::endl;
	std::cout << "=======================================" << std::endl;

	const auto patch_collection = eax::make_patch_collection();

	struct FoundApp
	{
		const eax::Patch* patch{};
		eax::FilePatcherUPtr patcher{};
	}; // FoundApp

	using FoundApps = std::vector<FoundApp>;
	auto found_apps = FoundApps{};
	found_apps.reserve(patch_collection.size());

	for (const auto& patch : patch_collection)
	{
		try
		{
			auto file_patcher = eax::make_file_patcher(patch);

			if (file_patcher->get_status() != eax::PatchStatus{})
			{
				found_apps.emplace_back();
				auto& found_app = found_apps.back();
				found_app.patch = &patch;
				found_app.patcher = std::move(file_patcher);
			}
		}
		catch (...)
		{
		}
	}

	if (found_apps.empty())
	{
		std::cerr << std::endl;
		std::cerr << "Not found any supported application." << std::endl << std::endl;

		get_input_string(press_enter_to_exit_message);

		return 1;
	}

	const auto found_app_count = static_cast<int>(found_apps.size());
	const auto is_one_app = (found_app_count == 1);

	auto app_show_index = 0;

	for (const auto& found_app : found_apps)
	{
		std::cout << std::endl;

		if (!is_one_app)
		{
			std::cout << (app_show_index + 1) << ')' << std::endl;
		}

		std::cout << "Application: " << found_app.patch->name << std::endl;

		const auto& patch_status_def = get_patch_status_def(found_app.patcher->get_status());
		std::cout << "Status: " << patch_status_def.name.c_str() << std::endl;

		std::cout << "Description: " << found_app.patch->description << std::endl;

		app_show_index += 1;
	}

	std::cout << std::endl << "--------------------------------------------------------" << std::endl;

	auto application_index = 0;

	if (found_app_count > 1)
	{
		std::cout <<
			std::endl <<
			"To select application type in it's number and press \"ENTER\"." << std::endl <<
			"To cancel just press \"ENTER\"." << std::endl
			<< std::endl;

		auto application_number = 0;

		while (true)
		{
			const auto answer = get_input_string("Application number: ");

			if (answer.empty())
			{
				throw AppPatcherCancelledException{};
			}

			try
			{
				application_number = std::stoi(answer);
			}
			catch (...)
			{
			}

			if (application_number > 0 && application_number <= found_app_count)
			{
				application_index = application_number - 1;
				break;
			}
		}
	}

	auto& found_app = found_apps[application_index];
	const auto& patch_status_def = get_patch_status_def(found_app.patcher->get_status());

	std::cout << std::endl;

	if (!is_one_app)
	{
		std::cout << "Selected application: " << found_app.patch->name << std::endl;
	}

	std::cout <<
		std::endl <<
		"To " <<
		patch_status_def.action.c_str() <<
		" type in `" <<
		patch_status_def.answer.c_str() <<
		"` and press `ENTER`." <<
		std::endl <<
		"To cancel just press \"ENTER\"." <<
		std::endl <<
		std::endl;

	while (true)
	{
		const auto answer = get_input_string("Action: ");

		if (answer == patch_status_def.answer)
		{
			break;
		}

		if (answer.empty())
		{
			throw AppPatcherCancelledException{};
		}
	}

	(found_app.patcher.get()->*patch_status_def.action_method)();

	std::cout << std::endl << "Succeeded." << std::endl << std::endl;

	get_input_string(press_enter_to_exit_message);

	return 0;
}
catch (const AppPatcherCancelledException&)
{
	std::cerr << std::endl << "Cancelled." << std::endl << std::endl;
	get_input_string(press_enter_to_exit_message);

	return 1;
}
catch (const std::exception& ex)
{
	std::cerr << std::endl << "[ERROR] " << ex.what() << std::endl << std::endl;
	get_input_string(press_enter_to_exit_message);

	return 1;
}
