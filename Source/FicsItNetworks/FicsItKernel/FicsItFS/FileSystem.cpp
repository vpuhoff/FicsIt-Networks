#include "FileSystem.h"

#include "CoreMinimal.h"

#include "FicsItNetworksModule.h"
#include "FileSystemSerializationInfo.h"
#include "Library/NodeName.h"

namespace FicsItKernel {
	namespace FicsItFS {
		bool Root::mount(FileSystem::SRef<FileSystem::Device> device, FileSystem::Path path) {
			// if device is DevDevice, search for existing DevDevice in mounts & prevent mount if found
			if (dynamic_cast<DevDevice*>(device.get())) {
				if (getDevDevice().isValid()) return false;
			}

			return FileSystemRoot::mount(device, path);
		}

		bool Root::unmount(FileSystem::Path path) {
			// check if mount is DevDevice & if it is, prevent unmount
			auto mount = mounts.find(path);
			if (mount != mounts.end() && dynamic_cast<DevDevice*>(mount->second.first.get())) return false;

			return FileSystemRoot::unmount(path);
		}

		bool Root::unmount(FileSystem::SRef<FileSystem::Device> device) {
			FileSystem::Path p;
			bool found = false;
			for (auto m : mounts) {
				if (m.second.first == device) {
					p = m.first;
					found = true;
					break;
				}
			}
			if (found) return unmount(p);
			return false;
		}

		std::int64_t Root::getMemoryUsage(bool recalc) {
			std::int64_t memoryUsage = 0;
			for (auto m : mounts) {
				FileSystem::SRef<FileSystem::MemDevice> tmpDev = m.second.first;
				if (tmpDev.isValid()) {
					if (recalc) memoryUsage += tmpDev->getSize();
					else memoryUsage += tmpDev->getUsed();
				}
			}
			return memoryUsage;
		}

		FileSystem::WRef<DevDevice> Root::getDevDevice() {
			for (auto& mount : mounts) {
				if (DevDevice* device = dynamic_cast<DevDevice*>(mount.second.first.get())) return device;
			}
			return nullptr;
		}

		FileSystem::Path Root::getMountPoint(FileSystem::SRef<DevDevice> device) {
			for (auto& mount : mounts) {
				if (device == mount.second.first) return mount.first;
			}
			return "";
		}

		std::string Root::persistPath(FileSystem::Path path) {
			FileSystem::Path pending;
			FileSystem::SRef<FileSystem::Device> dev = getDevice(path, pending);
			FileSystem::NodeName name = "";
			FileSystem::SRef<DevDevice> devDev = getDevDevice();
			if (dev != devDev) for (std::pair<const FileSystem::NodeName, FileSystem::SRef<FileSystem::Device>>& device : devDev->getDevices()) {
				if (device.second == dev) {
					name = device.first;
					break;
				}
			}
			if (name == "" && dev != devDev) throw std::exception("Unable to persist path");
			return name + ":" + pending.str();
		}

		FileSystem::Path Root::unpersistPath(std::string path) {
			size_t pos = path.find(':');
			FileSystem::NodeName name = path.substr(0, pos);
			FileSystem::Path pending = path.substr(pos+1);
			FileSystem::SRef<DevDevice> devDev = getDevDevice();
			FileSystem::SRef<FileSystem::Device> dev;
			if (pos == 0) {
				dev = devDev;
            } else for (std::pair<const FileSystem::NodeName, FileSystem::SRef<FileSystem::Device>>& device : devDev->getDevices()) {
				if (device.first == name) {
					dev = device.second;
					break;
				}
			}
			if (dev.isValid()) for (auto& mount : mounts) {
				if (mount.second.first == dev) {
					return mount.first / pending;
				}
			}
			throw std::invalid_argument("Unable to unpersist path");
		}

		bool Root::checkUnpersistPath(std::string path) {
			size_t pos = path.find(':');
			FileSystem::NodeName name = path.substr(0, pos);
			FileSystem::Path pending = path.substr(pos+1);
			FileSystem::SRef<DevDevice> devDev = getDevDevice();
			FileSystem::SRef<FileSystem::Device> dev;
			if (pos == 0) {
				dev = devDev;
			} else for (std::pair<const FileSystem::NodeName, FileSystem::SRef<FileSystem::Device>>& device : devDev->getDevices()) {
				if (device.first == name) {
					dev = device.second;
					break;
				}
			}
			if (dev.isValid()) for (auto& mount : mounts) {
				if (mount.second.first == dev) {
					return true;
				}
			}
			return false;
		}

		void Root::Serialize(FArchive& Ar, FFileSystemSerializationInfo& info) {
			if (Ar.IsSaving() && getDevDevice()) {
				// serialize mount points
				for (auto mount : mounts) {
					for (auto device : getDevDevice()->getDevices()) {
						if (mount.second.first == device.second) {
							info.Mounts.Add(device.first.c_str(), mount.first.str().c_str());
							break;
						}
					}
				}

				// serialize tempfs
				for (std::pair<const FileSystem::NodeName, FileSystem::SRef<FileSystem::Device>> dev : getDevDevice()->getDevices()) {
					if (!dynamic_cast<FileSystem::MemDevice*>(dev.second.get())) continue;
					FFileSystemNode node = FFileSystemNode().Serialize(dev.second, "/");
					node.NodeType = 3;
					info.Devices.Add(dev.first.c_str(), node);
				}
			}
			Ar << info.Mounts;
			Ar << info.Devices;
		}

		void Root::PostLoad(const FFileSystemSerializationInfo& info) {
			FileSystem::SRef<DevDevice> devDev = getDevDevice();
			if (!devDev.isValid()) return;
			
			// deserialize/generate tmpfs
			for (TPair<FString, FFileSystemNode> device : info.Devices) {
				std::string deviceName = TCHAR_TO_UTF8(*device.Key);
				if (device.Value.NodeType == 3) {
					FileSystem::SRef<FileSystem::Device> dev = new FileSystem::MemDevice();
					if (!devDev->addDevice(dev, deviceName)) {
						UE_LOG(LogFicsItNetworks, Error, TEXT("Unable to unpersist tmpfs '%s'"), *FString(deviceName.c_str()));
						continue;
					}
					device.Value.Deserialize(dev, deviceName);
				}
			}
			
			// lodd mounts
			for (TPair<FString, FString> mount : info.Mounts) {
				for (auto device : devDev->getDevices()) {
					if (FString(device.first.c_str()) == mount.Key) {
						this->mount(device.second, TCHAR_TO_UTF8(*mount.Value));
						break;
					}
				}
			}
		}
	}
}
