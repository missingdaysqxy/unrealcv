#include "TextureReader.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
// #include "Runtime/ShaderCore/Public/StaticBoundShaderState.h"
// #include "Runtime/ShaderCore/Public/GlobalShader.h"
#include "Runtime/Engine/Public/ScreenRendering.h"
#include "Runtime/Core/Public/Modules/ModuleManager.h"

#include "UnrealcvStats.h"
#include "UnrealcvLog.h"

DECLARE_CYCLE_STAT(TEXT("ResizeReadBufferFast"), STAT_ResizeReadBufferFast, STATGROUP_UnrealCV);

/**
ReadData from texture2D
This is the standard way in Unreal, but very slow
*/
bool ReadTextureRenderTarget(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& ImageData, int& Width, int& Height)
{
	if (RenderTarget == nullptr)
	{
		UE_LOG(LogUnrealCV, Warning, TEXT("The RenderTarget is nullptr"));
		return false;
	}
	// Get the RHI

	Width = RenderTarget->SizeX;
	Height = RenderTarget->SizeY;

	// Initialize the image data array
	ImageData.Empty();
	ImageData.AddZeroed(Width * Height);
	FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	{
		RenderTargetResource->ReadPixels(ImageData, ReadSurfaceDataFlags);
	}

	// Serialize and save png
	return true;
}

bool FastReadTexture2DAsync(FTexture2DRHIRef Texture2D, TFunction<void(FColor*, int32, int32)> Callback)
{
	auto RenderCommand = [=](FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef SrcTexture)
	{
		if (SrcTexture == nullptr)
		{
			UE_LOG(LogUnrealCV, Warning, TEXT("Input texture2D is nullptr"));
			return;
		}
		FRHIResourceCreateInfo CreateInfo;
		FTexture2DRHIRef ReadbackTexture = RHICreateTexture2D(
			SrcTexture->GetSizeX(), SrcTexture->GetSizeY(),
			EPixelFormat::PF_B8G8R8A8,
			// SrcTexture->GetFormat(),
			// EPixelFormat::PF_A32B32G32R32F,
			1, 1,
			TexCreate_CPUReadback,
			CreateInfo
		);

		if (ReadbackTexture->GetFormat() != SrcTexture->GetFormat())
		{
			UE_LOG(LogUnrealCV, Warning, TEXT("ReadbackTexture and SrcTexture are different"));
			return;
		}
		void* ColorDataBuffer = nullptr;
		int32 Width = 0, Height = 0;

		// Debug, check the data before and after the copy operation
		// RHICmdList.MapStagingSurface(ReadbackTexture, ColorDataBuffer, Width, Height);
		// RHICmdList.UnmapStagingSurface(ReadbackTexture);

		FResolveParams ResolveParams;
		RHICmdList.CopyToResolveTarget(
			SrcTexture,
			ReadbackTexture,
			FResolveParams());

		RHICmdList.MapStagingSurface(ReadbackTexture, ColorDataBuffer, Width, Height);

		FColor* ColorBuffer = reinterpret_cast<FColor*>(ColorDataBuffer);
		Callback(ColorBuffer, Width, Height);
		RHICmdList.UnmapStagingSurface(ReadbackTexture);
	};

	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
		FastReadBuffer,
		TFunction<void(FRHICommandListImmediate&, FTexture2DRHIRef)>, InRenderCommand, RenderCommand,
		FTexture2DRHIRef, InTexture2D, Texture2D,
		{
			InRenderCommand(RHICmdList, InTexture2D);
		});
	return true;
}