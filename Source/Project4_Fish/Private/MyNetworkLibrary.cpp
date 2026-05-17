// Fill out your copyright notice in the Description page of Project Settings.


#include "MyNetworkLibrary.h"

#include "Sockets.h"
#include "SocketSubsystem.h"

// ローカルIpv4アドレスを取得。
FString UMyNetworkLibrary::GetLocalIPv4()
{
    bool bCanBind = false;
    TSharedRef<FInternetAddr> Addr =
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)
        ->GetLocalHostAddr(*GLog, bCanBind);

    if (Addr->IsValid())
    {
        return Addr->ToString(false);
    }

    return FString("Invalid");
}