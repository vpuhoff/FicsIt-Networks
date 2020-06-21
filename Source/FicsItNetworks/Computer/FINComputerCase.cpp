#include "FINComputerCase.h"

#include "FicsItNetworksCustomVersion.h"
#include "FINComputerProcessor.h"
#include "FINComputerMemory.h"
#include "FINComputerDriveHolder.h"
#include "FicsItKernel/Processor/Lua/LuaProcessor.h"
#include "FGInventoryComponent.h"
#include "FINComputerDriveDesc.h"
#include "FINComputerEEPROMDesc.h"
#include "FINComputerFloppyDesc.h"
#include "FicsItKernel/FicsItKernel.h"
#include "util/Logging.h"

AFINComputerCase::AFINComputerCase() {
	NetworkConnector = CreateDefaultSubobject<UFINNetworkConnector>("NetworkConnector");
	NetworkConnector->AddMerged(this);
	NetworkConnector->SetupAttachment(RootComponent);
	NetworkConnector->OnNetworkSignal.AddDynamic(this, &AFINComputerCase::HandleSignal);
	
	Panel = CreateDefaultSubobject<UFINModuleSystemPanel>("Panel");
	Panel->SetupAttachment(RootComponent);
	Panel->OnModuleChanged.AddDynamic(this, &AFINComputerCase::OnModuleChanged);

	DataStorage = CreateDefaultSubobject<UFGInventoryComponent>("DataStorage");
	DataStorage->OnItemRemovedDelegate.AddDynamic(this, &AFINComputerCase::OnEEPROMChanged);
	DataStorage->OnItemAddedDelegate.AddDynamic(this, &AFINComputerCase::OnEEPROMChanged);
	DataStorage->mItemFilter.BindLambda([](TSubclassOf<UObject> item, int32 i) {
        SML::Logging::error("Uhm... ", TCHAR_TO_UTF8(*item->GetName()), " ", i);
        return (i == 0 && item->IsChildOf<UFINComputerEEPROMDesc>()) || (i == 1 && item->IsChildOf<UFINComputerFloppyDesc>());
    });
	
	mFactoryTickFunction.bCanEverTick = true;
	mFactoryTickFunction.bStartWithTickEnabled = true;
	mFactoryTickFunction.bRunOnAnyThread = true;
	mFactoryTickFunction.bAllowTickOnDedicatedServer = true;

	if (HasAuthority()) mFactoryTickFunction.SetTickFunctionEnable(true);

	kernel = new FicsItKernel::KernelSystem();
	kernel->setNetwork(new FicsItKernel::Network::NetworkController());
	kernel->getNetwork()->component = NetworkConnector;
}

AFINComputerCase::~AFINComputerCase() {
	if (kernel) delete kernel;
}

void AFINComputerCase::Serialize(FArchive& ar) {
	if (ar.IsSaveGame()) {
		ar.UsingCustomVersion(FFINCustomVersion::GUID);
		if (ar.CustomVer(FFINCustomVersion::GUID) >= FFINCustomVersion::KernelSystemPersistency) {
			ar << NetworkConnector;
			ar << Panel;
			ar << DataStorage;
			kernel->Serialize(ar, KernelState);
		} else {
			Super::Serialize(ar);
		}
	} else {
		Super::Serialize(ar);
	}
}

void AFINComputerCase::BeginPlay() {
	Super::BeginPlay();
	
	DataStorage->Resize(2);
}

void AFINComputerCase::Factory_Tick(float dt) {
	kernel->tick(dt);
}

bool AFINComputerCase::ShouldSave_Implementation() const {
	return true;
}

void AFINComputerCase::PreLoadGame_Implementation(int32 gameVersion, int32 engineVersion) {
	kernel->PreSerialize(KernelState, true);
}

void AFINComputerCase::PostLoadGame_Implementation(int32 saveVersion, int32 gameVersion) {
	TArray<AActor*> modules;
	Panel->GetModules(modules);
	AddModules(modules);
	
	kernel->PostSerialize(KernelState, true);
}

void AFINComputerCase::PreSaveGame_Implementation(int32 gameVersion, int32 engineVersion) {
	kernel->PreSerialize(KernelState, false);
}

void AFINComputerCase::PostSaveGame_Implementation(int32 gameVersion, int32 engineVersion) {
	kernel->PostSerialize(KernelState, false);
}

void AFINComputerCase::AddProcessor(AFINComputerProcessor* processor) {
	Processors.Add(processor);
	if (Processors.Num() == 1) {
		// no processor already added -> add new processor
		kernel->setProcessor(processor->CreateProcessor());
		kernel->getProcessor()->setEEPROM(UFINComputerEEPROMDesc::GetEEPROM(DataStorage, 0));
	} else {
		// processor already added
		kernel->setProcessor(nullptr);
	}
	if (Processors.Num() > 0) Panel->AllowedModules.Remove(AFINComputerProcessor::StaticClass());
	else Panel->AllowedModules.Add(AFINComputerProcessor::StaticClass());
}

void AFINComputerCase::RemoveProcessor(AFINComputerProcessor* processor) {
	Processors.Remove(processor);
	if (Processors.Num() == 1) {
		// two processors were added -> add leaving processor to kernel
		kernel->setProcessor((*Processors.Find(0))->CreateProcessor());
	} else {
		// more than two processors were added or no processor remaining -> remove processor from kernel
		kernel->setProcessor(nullptr);
	}
	
	if (Processors.Num() > 0) Panel->AllowedModules.Remove(AFINComputerProcessor::StaticClass());
	else Panel->AllowedModules.Add(AFINComputerProcessor::StaticClass());
}

void AFINComputerCase::AddMemory(AFINComputerMemory* memory) {
	Memories.Add(memory);
	RecalculateMemory();
}

void AFINComputerCase::RemoveMemory(AFINComputerMemory* memory) {
	Memories.Remove(memory);
	RecalculateMemory();
}

void AFINComputerCase::RecalculateMemory() {
	int64 capacity = 0;
	for (AFINComputerMemory* memory : Memories) {
		capacity += memory->GetCapacity();
	}
	kernel->setCapacity(capacity);
}

void AFINComputerCase::AddDrive(AFINComputerDriveHolder* DriveHolder) {
	if (DriveHolders.Contains(DriveHolder)) return;
	DriveHolders.Add(DriveHolder);
	DriveHolder->OnDriveUpdate.AddDynamic(this, &AFINComputerCase::OnDriveUpdate);
	AFINFileSystemState* FileSystemState = DriveHolder->GetDrive();
	if (FileSystemState) kernel->addDrive(FileSystemState);
}

void AFINComputerCase::RemoveDrive(AFINComputerDriveHolder* DriveHolder) {
	if (DriveHolders.Remove(DriveHolder) <= 0) return;
	DriveHolder->OnDriveUpdate.RemoveDynamic(this, &AFINComputerCase::OnDriveUpdate);
	AFINFileSystemState* FileSystemState = DriveHolder->GetDrive();
	if (FileSystemState) kernel->removeDrive(FileSystemState);
}

void AFINComputerCase::AddModule(AActor* module) {
	if (AFINComputerProcessor* processor = Cast<AFINComputerProcessor>(module)) {
		AddProcessor(processor);
	} else if (AFINComputerMemory* memory = Cast<AFINComputerMemory>(module)) {
		AddMemory(memory);
	} else if (AFINComputerDriveHolder* holder = Cast<AFINComputerDriveHolder>(module)) {
		AddDrive(holder);
	}
}

void AFINComputerCase::RemoveModule(AActor* module) {
	if (AFINComputerProcessor* processor = Cast<AFINComputerProcessor>(module)) {
		RemoveProcessor(processor);
	} else if (AFINComputerMemory* memory = Cast<AFINComputerMemory>(module)) {
		RemoveMemory(memory);
	} else if (AFINComputerDriveHolder* holder = Cast<AFINComputerDriveHolder>(module)) {
		RemoveDrive(holder);
	}
}

void AFINComputerCase::AddModules(const TArray<AActor*>& Modules) {
	for (AActor* Module : Modules) {
		AddModule(Module);
	}
}

void AFINComputerCase::OnModuleChanged(UObject* module, bool added) {
	if (module->Implements<UFINModuleSystemModule>()) {
		AActor* moduleActor = Cast<AActor>(module);
		if (added) AddModule(moduleActor);
		else RemoveModule(moduleActor);
	}
}

void AFINComputerCase::OnEEPROMChanged(TSubclassOf<UFGItemDescriptor> Item, int32 Num) {
	if (Item->IsChildOf<UFINComputerEEPROMDesc>()) {
		FicsItKernel::Processor* processor = kernel->getProcessor();
		if (processor) processor->setEEPROM(UFINComputerEEPROMDesc::GetEEPROM(DataStorage, 0));
	} else if (Item->IsChildOf<UFINComputerDriveDesc>()) {
		AFINFileSystemState* state = nullptr;
		FInventoryStack stack;
		if (DataStorage->GetStackFromIndex(1, stack)) {
			TSubclassOf<UFINComputerDriveDesc> driveDesc = stack.Item.ItemClass;
			state = Cast<AFINFileSystemState>(stack.Item.ItemState.Get());
			if (IsValid(driveDesc)) {
				if (!IsValid(state)) {
					state = AFINFileSystemState::CreateState(this, UFINComputerDriveDesc::GetStorageCapacity(driveDesc), DataStorage, 1);
				}
			}
		}
		if (IsValid(Floppy)) {
			kernel->removeDrive(Floppy);
			Floppy = nullptr;
		}
		if (IsValid(state)) {
			Floppy = state;
			kernel->addDrive(Floppy);
		}
	}
}

void AFINComputerCase::Toggle() {
	FicsItKernel::Processor* processor = kernel->getProcessor();
	if (processor) processor->setEEPROM(UFINComputerEEPROMDesc::GetEEPROM(DataStorage, 0));
	switch (kernel->getState()) {
	case FicsItKernel::KernelState::SHUTOFF:
		kernel->start(false);
		SerialOutput = "";
		break;
	case FicsItKernel::KernelState::CRASHED:
		kernel->start(true);
		SerialOutput = "";
		break;
	default:
		kernel->stop();	
		break;
	}
}

FString AFINComputerCase::GetCrash() {
	return UTF8_TO_TCHAR(kernel->getCrash().what());
}

EComputerState AFINComputerCase::GetState() {
	using State = FicsItKernel::KernelState;
	switch (kernel->getState()) {
	case State::RUNNING:
		return EComputerState::RUNNING;
	case State::SHUTOFF:
		return EComputerState::SHUTOFF;
	default:
		return EComputerState::CRASHED;
	}
}

FString AFINComputerCase::GetSerialOutput() {
	FileSystem::SRef<FicsItKernel::FicsItFS::DevDevice> dev = kernel->getDevDevice();
	if (dev) {
		SerialOutput = SerialOutput.Append(dev->getSerial()->readOutput().c_str());
		SerialOutput = SerialOutput.Right(1000);
	}
	return SerialOutput;
}

void AFINComputerCase::WriteSerialInput(const FString& str) {
	FileSystem::SRef<FicsItKernel::FicsItFS::DevDevice> dev = kernel->getDevDevice();
	if (dev) {
		dev->getSerial()->write(TCHAR_TO_UTF8(*str));
	}
}

void AFINComputerCase::HandleSignal(FFINSignal signal, FFINNetworkTrace sender) {
	if (kernel) kernel->getNetwork()->pushSignal(signal, sender);
}

void AFINComputerCase::OnDriveUpdate(bool added, AFINFileSystemState* drive) {
	if (added) {
		kernel->addDrive(drive);
	} else {
		kernel->removeDrive(drive);
	}
}
