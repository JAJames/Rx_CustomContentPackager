/**
 * Copyright (C) 2016 Jessica James.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Written by Jessica James <jessica.aj@outlook.com>
 */

#define _CRT_NONSTDC_NO_DEPRECATE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#if defined _WIN32
#include <Windows.h>
#endif // _WIN32

enum UDKPackage_Extension
{
	ext_UNKNOWN,
	ext_UDK,
	ext_UPK,
	ext_U
};

/** Package */
const char *package_filename = NULL;
uint32_t package_name = 0;
uint32_t package_GUID[4];
enum UDKPackage_Extension package_extension = ext_UNKNOWN;

/** Name table */
uint32_t name_table_size = 0;
char **name_table = NULL;

#define INVALID_NAME UINT32_MAX

/** Import table */
struct UDKImport
{
	uint32_t package_name_index;
	uint32_t class_name_index;
	int32_t package_reference;
	uint32_t object_name_index;
};
uint32_t import_table_size = 0;
struct UDKImport *import_table = NULL;
size_t packages_imported = 0;

/** Against list */
uint32_t against_list_size = 0;
uint32_t **against_list = NULL;

/** Package table */
struct UDKPackage
{
	uint32_t name_index;
	uint32_t GUID[4];
	char *filename;
	enum UDKPackage_Extension extension;
};
struct UDKPackage *package_table;

/** Dependency table */
struct UDKPackage_Dependency
{
	struct UDKPackage *package;
	struct UDKPackage_Dependency *next;
};
uint32_t dependency_list_size = 0;
struct UDKPackage_Dependency *dependency_list_head = NULL;
struct UDKPackage_Dependency *dependency_list_last = NULL;

/** Game package table */
struct UDKPackage_Game
{
	char *name;
	uint32_t GUID[4];

	struct UDKPackage_Game *next;
};
size_t game_package_table_size = 0;
struct UDKPackage_Game *game_package_table_head = NULL;
struct UDKPackage_Game *game_package_table_last = NULL;

/** Utility Functions */

const char *str_find_suffix(const char *str, const char *suffix)
{
	const char *str_end = str;
	const char *suffix_end = suffix;

	while (*str_end != '\0')
		++str_end;

	while (*suffix_end != '\0')
		++suffix_end;

	if (str_end - str < suffix_end - suffix) // Too short to contain suffix
		return NULL;

	while (suffix_end != suffix)
	{
		if (*--str_end != *--suffix_end)
			return NULL;
	}

	return str_end;
}

bool streql_2ptr(const char *filename, const char *filename_end, const char *package_name)
{
	while (filename != filename_end)
	{
		if (toupper(*filename) != toupper(*package_name)) // Names are case-insensitive
			return false;
		++filename, ++package_name;
	}
	return *package_name == '\0';
}

void read_guid(uint32_t *GUID, FILE *in_file)
{
	// seek to string size
	fseek(in_file, 0x0C, SEEK_SET);

	// read string size (repurpose GUID[0])
	fread(GUID, sizeof(uint32_t), 1, in_file);

	// seek to GUID
	fseek(in_file, *GUID + 0x30, SEEK_CUR);

	// read GUID
	fread(GUID, sizeof(uint32_t), 4, in_file);
}

void free_UDKPackage_Game(struct UDKPackage_Game *package)
{
	free(package->name);
	free(package);
}

enum UDKPackage_Extension get_extension_from_filename(const char *filename, size_t filename_length)
{
	filename += filename_length;
	while (filename_length != 0)
	{
		if (*--filename == '.') // start of extension
		{
			++filename;

			if (strcmpi(filename, "udk"))
				return ext_UDK;
			if (strcmpi(filename, "upk"))
				return ext_UPK;
			if (strcmpi(filename, "u"))
				return ext_U;

			return ext_UNKNOWN;
		}
		--filename_length;
	}
	return ext_UNKNOWN;
}

const char *extension_as_string(enum UDKPackage_Extension extension)
{
	switch (extension)
	{
	case ext_UDK:
		return "udk";
	case ext_UPK:
		return "upk";
	case ext_U:
		return "u";
	default:
		return "";
	}
}

/** Name Table Functions */

void read_name_table(FILE *file)
{
	uint32_t tmp;
	size_t index;

	// seek to string size
	fseek(file, 0x0C, SEEK_SET);

	// read string size
	fread(&tmp, sizeof(tmp), 1, file);

	// seek to name count
	fseek(file, tmp + 0x04, SEEK_CUR);

	// read name count
	fread(&name_table_size, sizeof(name_table_size), 1, file);

	// read name table offset
	fread(&tmp, sizeof(tmp), 1, file);

	// seek to name table
	fseek(file, tmp, SEEK_SET);

	// allocate array of char pointers
	if (name_table != NULL)
		free(name_table);
	name_table = (char **) malloc(sizeof(char *) * name_table_size);

	// read name table
	for (index = 0; index != name_table_size; ++index)
	{
		// read name length (includes null term)
		fread(&tmp, sizeof(tmp), 1, file);

		// allocate string buffer & copy string from file
		name_table[index] = (char *) malloc(sizeof(char) * tmp);
		fread(name_table[index], sizeof(char), tmp, file);

		// skip Object Flags
		fseek(file, 0x08, SEEK_CUR);
	}
}

void print_name_table(FILE *out)
{
	size_t index;

	for (index = 0; index != name_table_size; ++index)
		fprintf(out, "%u: %s\r\n", index, name_table[index]);
}

uint32_t find_name(const char *name)
{
	uint32_t index;

	for (index = 0; index != name_table_size; ++index)
		if (strcmp(name, name_table[index]) == 0)
			return index;

	return INVALID_NAME;
}

uint32_t find_name_2ptr(const char *name_start, const char *name_end)
{
	uint32_t index;

	for (index = 0; index != name_table_size; ++index)
		if (streql_2ptr(name_start, name_end, name_table[index]))
			return index;

	return INVALID_NAME;
}

uint32_t name_from_filename(const char *filename, size_t filename_length)
{
	const char *start_name = NULL;
	const char *end_name = NULL;

	filename += filename_length;
	while (filename_length != 0)
	{
		--filename;

		if (*filename == '.' && end_name == NULL)
			end_name = filename;

		if (*filename == '\\' || *filename == '/')
		{
			start_name = ++filename;
			break;
		}

		--filename_length;
	}

	if (start_name == NULL)
		start_name = filename;

	if (end_name == NULL)
		return find_name(start_name);

	return find_name_2ptr(start_name, end_name);
}

/** Import Table Functions */

void read_import_table(FILE *file)
{
	uint32_t tmp;

	// seek to string size
	fseek(file, 0x0C, SEEK_SET);

	// read string size
	fread(&tmp, sizeof(tmp), 1, file);

	// seek to import count
	fseek(file, tmp + 0x14, SEEK_CUR);

	// read import count
	fread(&import_table_size, sizeof(import_table_size), 1, file);

	// read import table offset
	fread(&tmp, sizeof(tmp), 1, file);

	// seek to import table
	fseek(file, tmp, SEEK_SET);

	// allocate array of UDKImport objects
	if (import_table != NULL)
		free(import_table);
	import_table = (struct UDKImport *) malloc(sizeof(struct UDKImport) * import_table_size);

	// read import table
	// fread(import_table + tmp, sizeof(struct UDKImport), import_table_size, file);
	for (tmp = 0; tmp != import_table_size; ++tmp)
	{
		// Seek after read because:
		//	"Name indexes work the same way as #Index but since Unreal Engine 3 indexes referencing a name, have a another Int32 followed after the index."
		// Source: http://eliotvu.com/page/unreal-package-file-format

		fread(&import_table[tmp].package_name_index, sizeof(uint32_t), 1, file);
		fseek(file, 0x04, SEEK_CUR);

		fread(&import_table[tmp].class_name_index, sizeof(uint32_t), 1, file);
		fseek(file, 0x04, SEEK_CUR);

		fread(&import_table[tmp].package_reference, sizeof(int32_t), 1, file);

		fread(&import_table[tmp].object_name_index, sizeof(uint32_t), 1, file);
		fseek(file, 0x04, SEEK_CUR);

		if (import_table[tmp].package_reference == 0)
			++packages_imported;
	}
}

void print_import_table(FILE *out)
{
	size_t index;
	struct UDKImport *itr = import_table;

	for (index = 0; index != import_table_size; ++index, ++itr)
		fprintf(out, "%u | Package: %s | Class: %s | Object: %s | Reference: %d\r\n", index, name_table[itr->package_name_index], name_table[itr->class_name_index], name_table[itr->object_name_index], itr->package_reference);
} 

/** Against list */

void read_against_list(FILE *against_file)
{
	uint32_t tmp;

	fread(&against_list_size, sizeof(against_list_size), 1, against_file);
	against_list = (uint32_t **) malloc(sizeof(uint32_t *) * against_list_size);

	for (tmp = 0; tmp != against_list_size; ++tmp)
	{
		against_list[tmp] = (uint32_t *) malloc(sizeof(uint32_t) * 4);
		fread(against_list[tmp], sizeof(uint32_t), 4, against_file);
	}
}

void build_against_list()
{
	struct UDKPackage_Game *package = game_package_table_head;
	uint32_t **itr;

	against_list_size = game_package_table_size;
	against_list = (uint32_t **) malloc(sizeof(uint32_t *) * against_list_size);
	itr = against_list;

	while (package != NULL)
	{
		*itr = (uint32_t *) malloc(sizeof(uint32_t) * 4);

		memcpy(*itr, package->GUID, sizeof(uint32_t) * 4);

		++itr;
		package = package->next;
	}
}

void write_against_list(FILE *against_file)
{
	uint32_t tmp;

	fwrite(&against_list_size, sizeof(against_list_size), 1, against_file);

	for (tmp = 0; tmp != against_list_size; ++tmp)
		fwrite(against_list[tmp], sizeof(uint32_t), 4, against_file);
}

bool is_in_against_list(uint32_t *GUID)
{
	uint32_t tmp;

	for (tmp = 0; tmp != against_list_size; ++tmp)
		if (memcmp(GUID, against_list[tmp], sizeof(uint32_t) * 4) == 0)
			return true;

	return false;
}

/** Package Table Functions */

void init_package_table()
{
	struct UDKPackage *itr;
	struct UDKPackage *end;
	size_t index = 0;

	package_table = (struct UDKPackage *) malloc(sizeof(struct UDKPackage) * packages_imported);
	itr = package_table;
	end = package_table + packages_imported;

	while (itr != end)
	{
		if (import_table[index].package_reference == 0)
		{
			memset(itr->GUID, 0, sizeof(itr->GUID));
			itr->name_index = import_table[index].object_name_index;
			itr->filename = NULL;
			itr->extension = ext_UNKNOWN;
			++itr;
		}
		++index;
	}
}

#if defined _WIN32

bool build_package_table(const char *directory)
{
	WIN32_FIND_DATA file_data;
	HANDLE find_handle;
	size_t directory_length = 0;
	char *tmp;
	size_t tmp_length;
	size_t tmp_index;
	FILE *tmp_file;
	enum UDKPackage_Extension extension;

	find_handle = FindFirstFile(directory, &file_data);

	if (find_handle == INVALID_HANDLE_VALUE)
		return false; // Error: Bad handle

	do
	{
		if (file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (file_data.cFileName[0] != '.')
			{
				// Calculate string lengths
				if (directory_length == 0)
					directory_length = strlen(directory) - 1;
				tmp_length = strlen(file_data.cFileName);

				tmp = (char *)malloc(sizeof(char) * (directory_length + tmp_length + 3));

				memcpy(tmp, directory, directory_length);
				memcpy(tmp + directory_length, file_data.cFileName, tmp_length);

				// Append wildcard and NULL terminator
				tmp_length += directory_length;
				tmp[tmp_length] = '\\';
				tmp[tmp_length + 1] = '*';
				tmp[tmp_length + 2] = '\0';

				build_package_table(tmp);
				free(tmp);
			}
		}
		else
		{
			tmp = (char *) str_find_suffix(file_data.cFileName, ".upk");
			if (tmp != NULL)
				extension = ext_UPK;
			else
			{
				tmp = (char *) str_find_suffix(file_data.cFileName, ".udk");
				if (tmp != NULL)
					extension = ext_UDK;
				else
				{
					tmp = (char *) str_find_suffix(file_data.cFileName, ".u");
					if (tmp != NULL)
						extension = ext_U;
					else
						extension = ext_UNKNOWN;
				}
			}

			if (tmp != NULL)
			{
				// check if package name matches a package in the table
				for (tmp_index = 0; tmp_index != packages_imported; ++tmp_index)
				{
					if (streql_2ptr(file_data.cFileName, tmp, name_table[package_table[tmp_index].name_index]))
					{
						// Calculate string lengths
						if (directory_length == 0)
							directory_length = strlen(directory) - 1;
						tmp_length = strlen(file_data.cFileName);

						package_table[tmp_index].extension = extension;

						package_table[tmp_index].filename = (char *) malloc(sizeof(char) * (directory_length + tmp_length + 1));

						memcpy(package_table[tmp_index].filename, directory, directory_length);
						memcpy(package_table[tmp_index].filename + directory_length, file_data.cFileName, tmp_length);
						package_table[tmp_index].filename[directory_length + tmp_length] = '\0';

						tmp_file = fopen(package_table[tmp_index].filename, "rb");
						if (tmp_file != NULL)
						{
							read_guid(package_table[tmp_index].GUID, tmp_file);
							fclose(tmp_file);
						}
						break;
					}
				}
			}
		}
	}
	while (FindNextFile(find_handle, &file_data));

	FindClose(find_handle);
	return true;
}

#endif // _WIN32

void print_package_table(FILE *out)
{
	size_t index;
	struct UDKPackage *itr = package_table;

	for (index = 0; index != packages_imported; ++index, ++itr)
	{
		fprintf(out, "%.8X%.8X%.8X%.8X | ", itr->GUID[0], itr->GUID[1], itr->GUID[2], itr->GUID[3]);
		fputs(name_table[itr->name_index], out);
		fputc('\n', out);
	}
}

/** Dependency Table Functions */

void build_dependency_list()
{
	size_t index;
	struct UDKPackage_Dependency *dependency;

	for (index = 0; index != packages_imported; ++index)
		if (is_in_against_list(package_table[index].GUID) == false)
		{
			dependency = (struct UDKPackage_Dependency *) malloc(sizeof(struct UDKPackage_Dependency));
			dependency->next = NULL;
			dependency->package = &package_table[index];

			if (dependency_list_last != NULL)
				dependency_list_last->next = dependency;
			else
				dependency_list_head = dependency;

			dependency_list_last = dependency;
			++dependency_list_size;
		}
}

void write_dependency_list(FILE *out)
{
	struct UDKPackage_Dependency *itr;

	fwrite(&dependency_list_size, sizeof(uint32_t), 1, out);
	for (itr = dependency_list_head; itr != NULL; itr = itr->next)
		fwrite(itr->package->GUID, sizeof(uint32_t), 4, out);
}

void print_dependency_list(FILE *out)
{
	struct UDKPackage_Dependency *itr = dependency_list_head;

	fprintf(out, "%u dependencies:\n", dependency_list_size);
	while (itr != NULL)
	{
		fprintf(out, "%.8X%.8X%.8X%.8X | ", itr->package->GUID[0], itr->package->GUID[1], itr->package->GUID[2], itr->package->GUID[3]);
		fputs(name_table[itr->package->name_index], out);
		fputs(" | ", out);
		fputs(itr->package->filename, out);
		fputc('\n', out);

		itr = itr->next;
	}
}

/** Game Package Table Functions */

#if defined _WIN32

struct UDKPackage_Game *add_UDKPackage_Game(const char *name, const char *name_end)
{
	struct UDKPackage_Game *ret;

	ret = (struct UDKPackage_Game *) malloc(sizeof(struct UDKPackage_Game));
	ret->next = NULL;
	ret->name = (char *) malloc(sizeof(char) * (name_end - name + 1));
	
	ret->name += name_end - name;
	*ret->name = '\0';
	while (name_end != name)
		*--ret->name = *--name_end;

	if (game_package_table_last != NULL)
		game_package_table_last->next = ret;
	else
		game_package_table_head = ret;

	game_package_table_last = ret;
	++game_package_table_size;

	return ret;
}

bool build_game_package_table(const char *directory)
{
	WIN32_FIND_DATA file_data;
	HANDLE find_handle;
	size_t directory_length = 0;
	char *tmp;
	size_t tmp_length;
	FILE *tmp_file;
	struct UDKPackage_Game *package;

	find_handle = FindFirstFile(directory, &file_data);

	if (find_handle == INVALID_HANDLE_VALUE)
		return false; // Error: Bad handle

	do
	{
		if (file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (file_data.cFileName[0] != '.')
			{
				// Calculate string lengths
				if (directory_length == 0)
					directory_length = strlen(directory) - 1;
				tmp_length = strlen(file_data.cFileName);

				tmp = (char *)malloc(sizeof(char) * (directory_length + tmp_length + 3));

				memcpy(tmp, directory, directory_length);
				memcpy(tmp + directory_length, file_data.cFileName, tmp_length);

				// Append wildcard and NULL terminator
				tmp_length += directory_length;
				tmp[tmp_length] = '\\';
				tmp[tmp_length + 1] = '*';
				tmp[tmp_length + 2] = '\0';

				build_game_package_table(tmp);
				free(tmp);
			}
		}
		else
		{
			tmp = (char *)str_find_suffix(file_data.cFileName, ".upk");
			if (tmp == NULL)
			{
				tmp = (char *)str_find_suffix(file_data.cFileName, ".udk");
				if (tmp == NULL)
					tmp = (char *)str_find_suffix(file_data.cFileName, ".u");
			}

			if (tmp != NULL)
			{
				package = add_UDKPackage_Game(file_data.cFileName, tmp);

				// Calculate string lengths
				if (directory_length == 0)
					directory_length = strlen(directory) - 1;
				tmp_length = strlen(file_data.cFileName);

				tmp = (char *)malloc(sizeof(char) * (directory_length + tmp_length + 1));

				memcpy(tmp, directory, directory_length);
				memcpy(tmp + directory_length, file_data.cFileName, tmp_length);
				tmp[directory_length + tmp_length] = '\0';

				tmp_file = fopen(tmp, "rb");
				if (tmp_file != NULL)
				{
					read_guid(package->GUID, tmp_file);
					fclose(tmp_file);
				}

				free(tmp);
			}
		}
	} while (FindNextFile(find_handle, &file_data));

	FindClose(find_handle);
	return true;
}

#endif // _WIN32

void print_game_package_table(FILE *out)
{
	struct UDKPackage_Game *itr = game_package_table_head;

	while (itr != NULL)
	{
		fprintf(out, "%.8X%.8X%.8X%.8X | ", itr->GUID[0], itr->GUID[1], itr->GUID[2], itr->GUID[3]);
		fputs(itr->name, out);
		fputc('\n', out);
		itr = itr->next;
	}
}

/** Packager */

void generate_package(const char *game_path)
{
	struct UDKPackage_Dependency *itr = dependency_list_head;
	char tmp[1024]; // 32 (directory) + 1 ('\') + 32 (filename) + 4 (".uxx") + 1 ('\0') = 70
	char tmp2[1024];
	size_t tmp_length = 0;

	tmp_length = sprintf(tmp, "%.8X%.8X%.8X%.8X", package_GUID[0], package_GUID[1], package_GUID[2], package_GUID[3]);
	CreateDirectory(tmp, NULL);

	tmp_length += sprintf(tmp + tmp_length, "\\UDKGame");
	CreateDirectory(tmp, NULL);

	// Copy config file
	sprintf(tmp + tmp_length, "\\Config");
	CreateDirectory(tmp, NULL);
	sprintf(tmp + tmp_length + 7, "\\%s.ini", name_table[package_name]);
	sprintf(tmp2, "%s\\Config\\%s.ini", game_path, name_table[package_name]);
	CopyFile(tmp2, tmp, false);

	tmp_length += sprintf(tmp + tmp_length, "\\CookedPC");
	CreateDirectory(tmp, NULL);

	tmp_length += sprintf(tmp + tmp_length, "\\Custom_Content");
	CreateDirectory(tmp, NULL);

	// Copy base package
	sprintf(tmp + tmp_length, "\\%s.%s", name_table[package_name], extension_as_string(package_extension));
	CopyFile(package_filename, tmp, false);

	while (itr != NULL) // Copy dependencies
	{
		sprintf(tmp + tmp_length, "\\%s.%s", name_table[itr->package->name_index], extension_as_string(itr->package->extension));
		CopyFile(itr->package->filename, tmp, false);
		itr = itr->next;
	}
}

/** Main (Entry Point) */

int main(int argc, const char **args)
{
	const char *game_path = "";
	const char *names_out = NULL;
	const char *imports_out = NULL;
	const char *dependencies_out = NULL;
	const char *against_in = NULL;
	const char *packages_out = NULL;
	const char *game_packages_out = NULL;
	const char *against_out = NULL;
	char *search_path = NULL;
	bool build_package = false;
	FILE *base_package = NULL;
	FILE *tmp_file = NULL;
	size_t index;

	if (argc < 2 || strcmp(args[1], "-help") == 0 || strcmp(args[1], "/?") == 0)
	{
		puts("[-in=\"\"] [-game-path=\"*\"] [-package] [-names=\"\"] [-imports=\"\"] [-dependencies=\"\"] [-against=\"\"] [-packages=\"\"] [-game-packages=\"\"] [-build-against=\"\"]");
		return 0;
	}

	for (index = 1; index != argc; ++index)
	{
		if (strcmp(args[index], "-in") == 0 || strcmp(args[index], "-level") == 0 || strcmp(args[index], "-map") == 0 || strcmp(args[index], "-file") == 0 || strcmp(args[index], "-filename") == 0)
			package_filename = args[++index];
		else if (strcmp(args[index], "-game-path") == 0)
			game_path = args[++index];
		else if (strcmp(args[index], "-names") == 0)
			names_out = args[++index];
		else if (strcmp(args[index], "-imports") == 0)
			imports_out = args[++index];
		else if (strcmp(args[index], "-dependencies") == 0)
			dependencies_out = args[++index];
		else if (strcmp(args[index], "-against") == 0)
			against_in = args[++index];
		else if (strcmp(args[index], "-packages") == 0)
			packages_out = args[++index];
		else if (strcmp(args[index], "-game-packages") == 0)
			game_packages_out = args[++index];
		else if (strcmp(args[index], "-build-against") == 0)
			against_out = args[++index];
		else if (strcmp(args[index], "-package") == 0)
			build_package = true;
	}

	if (against_in != NULL)
	{
		tmp_file = fopen(against_in, "rb");
		if (tmp_file != NULL)
		{
			read_against_list(tmp_file);
			fclose(tmp_file);
		}
	}

	if (game_path != NULL)
	{
		search_path = (char *) malloc(sizeof(char) * strlen(game_path) + 3);
		sprintf(search_path, "%s\\*", game_path);
	}

	if (package_filename != NULL)
	{
		base_package = fopen(package_filename, "rb");
		if (base_package == NULL)
		{
			puts("ERROR: UNABLE TO OPEN FILE.");
			return 0;
		}

		package_extension = get_extension_from_filename(package_filename, strlen(package_filename));

		read_guid(package_GUID, base_package);
		read_name_table(base_package);
		read_import_table(base_package);

		package_name = name_from_filename(package_filename, strlen(package_filename));

		fclose(base_package);

		init_package_table();
		build_package_table(search_path == NULL ? "*": search_path);

		if (build_package || dependencies_out != NULL)
			build_dependency_list();

		if (build_package)
			generate_package(game_path);
	}

	if (game_packages_out != NULL || against_out != NULL)
		build_game_package_table(search_path == NULL ? "*" : search_path);

	if (against_out != NULL)
		build_against_list();

	/** Write requested data */

	if (names_out != NULL)
	{
		tmp_file = fopen(names_out, "wb");
		if (tmp_file != NULL)
		{
			print_name_table(tmp_file);
			fclose(tmp_file);
		}
		else
			puts("ERROR: Unable to write name table.");
	}

	if (imports_out != NULL)
	{
		tmp_file = fopen(imports_out, "wb");
		if (tmp_file != NULL)
		{
			print_import_table(tmp_file);
			fclose(tmp_file);

			printf("%u import table entries written.\n", import_table_size);
		}
		else
			puts("ERROR: Unable to write import table.");
	}

	if (dependencies_out != NULL)
	{
		tmp_file = fopen(dependencies_out, "wb");
		if (tmp_file != NULL)
		{
			print_dependency_list(tmp_file);
			fclose(tmp_file);
		}
		else
			puts("ERROR: Unable to write dependency list.");
	}

	if (packages_out != NULL)
	{
		tmp_file = fopen(packages_out, "wb");
		if (tmp_file != NULL)
		{
			print_package_table(tmp_file);
			fclose(tmp_file);
		}
		else
			puts("ERROR: Unable to write package table");
	}

	if (game_packages_out != NULL)
	{
		tmp_file = fopen(game_packages_out, "wb");
		if (tmp_file != NULL)
		{
			print_game_package_table(tmp_file);
			fclose(tmp_file);
		}
		else
			puts("ERROR: Unable to write game package table");
	}

	if (against_out != NULL)
	{
		tmp_file = fopen(against_out, "wb");
		if (tmp_file != NULL)
		{
			write_against_list(tmp_file);
			fclose(tmp_file);
		}
		else
			puts("ERROR: Unable to write against list");
	}

	return 0;
}
