// Copyright (C) Gaijin Games KFT.  All rights reserved.

#import <Cocoa/Cocoa.h>
#import <Appkit/AppKit.h>
#import <objc/runtime.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkInterface.h>
#include <IOKit/network/IOEthernetController.h>
#include <Foundation/NSProcessInfo.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <paths.h>

bool macosx_get_desktop_res(int &wd, int &ht)
{
  wd = CGDisplayPixelsWide(CGMainDisplayID());
  ht = CGDisplayPixelsHigh(CGMainDisplayID());
  return true;
}

static bool findEthernetInterfaces(io_iterator_t *matchingServices)
{
  if (CFMutableDictionaryRef matchingDict = IOServiceMatching(kIOEthernetInterfaceClass))
    if (CFMutableDictionaryRef propertyMatchDict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
          &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks))
    {
      CFDictionarySetValue(propertyMatchDict, CFSTR(kIOPrimaryInterface), kCFBooleanTrue);
      CFDictionarySetValue(matchingDict, CFSTR(kIOPropertyMatchKey), propertyMatchDict);
      CFRelease(propertyMatchDict);
      constexpr mach_port_t defaultMasterPort = 0;
      return IOServiceGetMatchingServices(defaultMasterPort, matchingDict, matchingServices) == KERN_SUCCESS;
    }
  return false;
}

const char *macosx_get_def_mac_address(unsigned char *MACAddress)
{
  memset(MACAddress, 0, sizeof(kIOEthernetAddressSize));
  io_iterator_t intfIterator;
  if (!findEthernetInterfaces(&intfIterator))
    return NULL;

  static char def_adapter_model[256] = {0};
  def_adapter_model[0] = 0;
  while (io_object_t intfService = IOIteratorNext(intfIterator))
  {
    io_object_t controllerService;
    if (IORegistryEntryGetParentEntry(intfService, kIOServicePlane, &controllerService) == KERN_SUCCESS)
    {
      if (CFTypeRef MACAddressAsCFData = IORegistryEntryCreateCFProperty(controllerService,
            CFSTR(kIOMACAddress), kCFAllocatorDefault, 0))
      {
        CFDataGetBytes((CFDataRef)MACAddressAsCFData, CFRangeMake(0, kIOEthernetAddressSize), (UInt8*)MACAddress);
        CFRelease(MACAddressAsCFData);
      }
      if (CFStringRef data = (CFStringRef)IORegistryEntryCreateCFProperty(controllerService,
            CFSTR(kIOVendor), kCFAllocatorDefault, 0))
      {
        CFStringGetCString(data, def_adapter_model, sizeof(def_adapter_model)-1, kCFStringEncodingUTF8);
        CFRelease(data);
      }
      if (CFStringRef data = (CFStringRef)IORegistryEntryCreateCFProperty(controllerService,
            CFSTR(kIOModel), kCFAllocatorDefault, 0))
      {
        strcat(def_adapter_model, ":");
        int len = strlen(def_adapter_model);
        CFStringGetCString(data, def_adapter_model+len, sizeof(def_adapter_model)-len, kCFStringEncodingUTF8);
        CFRelease(data);
      }
      (void) IOObjectRelease(controllerService);
    }
    (void) IOObjectRelease(intfService);
  }
  (void) IOObjectRelease(intfIterator);
  return def_adapter_model;
}

const char *macosx_get_os_ver()
{
  return [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String];
}

int64_t macosx_get_phys_mem()
{
  return [[NSProcessInfo processInfo] physicalMemory];
}

int64_t macosx_get_virt_mem()
{
  struct xsw_usage vmusage = {0};
  size_t size = sizeof(vmusage);
  if (sysctlbyname("vm.swapusage", &vmusage, &size, NULL, 0)!=0)
    vmusage.xsu_total = 0;
  return macosx_get_phys_mem()+vmusage.xsu_total;
}


const char* macosx_get_location()
{
  static char location[4] = {0};
  const char* code = [[[NSLocale currentLocale] countryCode] UTF8String];

  if (code && code[0] && code[1])
  {
    location[0] = ::toupper(code[0]);
    location[1] = ::toupper(code[1]);
    location[2] = 0;
  }

  return location;
}

// 1 = rotational, 0 = solid state, -1 = not known
int macosx_get_disk_rotational(const char *path)
{
  struct statfs statf;
  if (statfs(path, &statf) != 0)
    return -1;
  const char *bsdName = statf.f_mntfromname;
  if (strncmp(bsdName, _PATH_DEV, strlen(_PATH_DEV)) == 0)
    bsdName += strlen(_PATH_DEV);

  constexpr mach_port_t defaultMasterPort = 0;
  CFMutableDictionaryRef matching = IOBSDNameMatching(defaultMasterPort, 0, bsdName);
  if (!matching)
    return -1;
  io_service_t media = IOServiceGetMatchingService(defaultMasterPort, matching); // consumes matching
  if (!media)
    return -1;

  // the medium type sits on the block storage device, several parents above an APFS volume
  int rotational = -1;
  if (CFTypeRef characteristics = IORegistryEntrySearchCFProperty(media, kIOServicePlane,
        CFSTR(kIOPropertyDeviceCharacteristicsKey), kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents))
  {
    if (CFGetTypeID(characteristics) == CFDictionaryGetTypeID())
    {
      CFTypeRef medium = CFDictionaryGetValue((CFDictionaryRef)characteristics, CFSTR(kIOPropertyMediumTypeKey));
      if (medium && CFGetTypeID(medium) == CFStringGetTypeID())
      {
        if (CFEqual(medium, CFSTR(kIOPropertyMediumTypeSolidStateKey)))
          rotational = 0;
        else if (CFEqual(medium, CFSTR(kIOPropertyMediumTypeRotationalKey)))
          rotational = 1;
      }
    }
    CFRelease(characteristics);
  }
  IOObjectRelease(media);
  return rotational;
}
