// ArcGISRestProvider.cpp - ArcGIS REST API 数据源实现
#include "Data/ArcGISRestProvider.h"
#include "Data/GeoJsonParser.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/Event.h"

bool UArcGISRestProvider::QueryFeatures(const FGeoRect& Bounds, TArray<FGISFeature>& OutFeatures)
{
    if (FeatureServiceUrl.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ArcGISRestProvider: FeatureServiceUrl is empty"));
        return false;
    }

    // 查询主图层
    bool bSuccess = QuerySingleLayer(FeatureServiceUrl, Bounds, OutFeatures);

    // 查询附加图层
    for (const FString& LayerUrl : AdditionalLayerUrls)
    {
        if (!LayerUrl.IsEmpty())
        {
            QuerySingleLayer(LayerUrl, Bounds, OutFeatures);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("ArcGISRestProvider: Queried %d total features"), OutFeatures.Num());
    return bSuccess;
}

bool UArcGISRestProvider::QuerySingleLayer(
    const FString& LayerUrl,
    const FGeoRect& Bounds,
    TArray<FGISFeature>& OutFeatures)
{
    int32 ResultOffset = 0;
    bool bHasMore = true;

    // 在循环外创建一次 Parser，避免每页都 NewObject 增加 GC 压力
    UGeoJsonParser* Parser = NewObject<UGeoJsonParser>();

    while (bHasMore)
    {
        const FString QueryUrl = BuildQueryUrl(LayerUrl, Bounds, ResultOffset);

        FString Response;
        if (!HttpGetSync(QueryUrl, Response))
        {
            UE_LOG(LogTemp, Error, TEXT("ArcGISRestProvider: HTTP request failed for %s"), *LayerUrl);
            if (ResultOffset > 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("ArcGISRestProvider: Returning partial data (%d features fetched before failure)"), ResultOffset);
            }
            return ResultOffset > 0;
        }

        // 解析 GeoJSON 响应
        TArray<FGISFeature> PageFeatures;
        if (!Parser->ParseString(Response, PageFeatures))
        {
            UE_LOG(LogTemp, Error, TEXT("ArcGISRestProvider: Failed to parse GeoJSON response"));
            if (ResultOffset > 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("ArcGISRestProvider: Returning partial data (%d features fetched before parse failure)"), ResultOffset);
            }
            return ResultOffset > 0;
        }

        OutFeatures.Append(PageFeatures);

        // 分页：如果返回数 == MaxRecordCount，可能还有更多
        if (PageFeatures.Num() >= MaxRecordCount)
        {
            ResultOffset += PageFeatures.Num();
            UE_LOG(LogTemp, Log, TEXT("ArcGISRestProvider: Page %d features, fetching more (offset=%d)"),
                PageFeatures.Num(), ResultOffset);
        }
        else
        {
            bHasMore = false;
        }
    }

    return true;
}

FString UArcGISRestProvider::BuildQueryUrl(
    const FString& LayerUrl,
    const FGeoRect& Bounds,
    int32 ResultOffset) const
{
    // 基础查询参数
    FString Url = FString::Printf(
        TEXT("%s/query?where=%s&outFields=%s&f=geojson&resultRecordCount=%d"),
        *LayerUrl,
        *FGenericPlatformHttp::UrlEncode(WhereClause),
        *FGenericPlatformHttp::UrlEncode(OutFields),
        MaxRecordCount
    );

    // 空间过滤
    if (Bounds.IsValid())
    {
        Url += FString::Printf(
            TEXT("&geometry=%f,%f,%f,%f&geometryType=esriGeometryEnvelope&spatialRel=esriSpatialRelIntersects&inSR=4326"),
            Bounds.MinLon, Bounds.MinLat, Bounds.MaxLon, Bounds.MaxLat
        );
    }

    // 分页
    if (ResultOffset > 0)
    {
        Url += FString::Printf(TEXT("&resultOffset=%d"), ResultOffset);
    }

    // API Key
    if (!ApiKey.IsEmpty())
    {
        Url += FString::Printf(TEXT("&token=%s"), *ApiKey);
    }

    return Url;
}

bool UArcGISRestProvider::HttpGetSync(const FString& Url, FString& OutResponse) const
{
    FHttpModule& HttpModule = FHttpModule::Get();
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = HttpModule.CreateRequest();

    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    // 同步等待（Editor 时使用，Runtime 应改为异步）
    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
    bool bRequestSuccess = false;

    Request->OnProcessRequestComplete().BindLambda(
        [&OutResponse, &bRequestSuccess, CompletionEvent]
        (FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
    {
        if (bSuccess && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
        {
            OutResponse = Response->GetContentAsString();
            bRequestSuccess = true;
        }
        CompletionEvent->Trigger();
    });

    Request->ProcessRequest();

    // 等待最多 30 秒（30000ms）
    constexpr uint32 HttpTimeoutMs = 30000;
    CompletionEvent->Wait(HttpTimeoutMs);
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    return bRequestSuccess;
}

bool UArcGISRestProvider::IsAvailable() const
{
    return !FeatureServiceUrl.IsEmpty();
}
