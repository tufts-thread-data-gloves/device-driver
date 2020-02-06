# Salmon Team Smart Gloves Device Driver

## Directions for building the code in Visual Studio on Windows 10
1. Clone down the code from github and open the solution file in Visual Studio
2. Make sure you have an updated version of the Windows 10 SDK on your computer.
	Anything since and including version 10.0.16299.0 should work fine. This can be found in 
	the directory 'C:\Program Files (x86)\Windows Kits\10\UnionMetadata\'
3. Download the boost c++ library from https://www.boost.org/users/download/ and install it so that it
	looks similar to 'C:\Program Files\boost\boost_1_55_0'
4. In visual studio code in the Solution Explorer, right click on the 'ThreadDataGlovesDeviceDriver' project
	and click on properties
5. Under properties->c/c++->general, change consume windows runtime extension to yes
6. Under properties->c/c++->code generation, turn conformance mode off
7. Under properties->c/c++->general, add the following as using directories:
	- C:\Program Files (x86)\Windows Kits\10\References\CommonConfiguration\Neutral\Annotate
	- C:\Program Files (x86)\Windows Kits\10\UnionMetadata\{version of Windows 10 SDK}
	- C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\VC\vcpackages
8. Under properties->c/c++->general, add the location of the boost library as an include directory. 
	(ex: C:\Program Files\boost\boost_1_55_0)
9. Under properties->c/c++->Precompiled Headers change 'Precompiled Header' field to 
	'Not using precompiled headers'
10. Apply and save the properties
11. Close visual studio
12. Restart your computer for the changes to take effect

The solution should now build and be able to connect to a salmon glove.

## Common Problems
1. If you see issues in building that have to do with co await and concurrency tasks, 
make sure that your C++ version is set to C++11.
2. If you see linker errors with windows socket functions, this Stack Overflow question may help:
https://stackoverflow.com/questions/17069802/c-winsock2-errors?fbclid=IwAR0BaGDmQJed-hFH1J6fGDvtTHks_zzIuVUbrBlZ_ccfpWi57NdX9a9VBAo

