# Yule server image. Build context is the server/ folder (see broker/docker-compose.yml).
#   docker build -t yule-server:local .

FROM mcr.microsoft.com/dotnet/sdk:10.0 AS build
WORKDIR /src
# Restore first, so dependency layers are cached when only source changes.
COPY src/Yule.Server/Yule.Server.csproj src/Yule.Server/
RUN dotnet restore src/Yule.Server/Yule.Server.csproj
COPY src/ src/
RUN dotnet publish src/Yule.Server/Yule.Server.csproj -c Release -o /app/publish --no-restore /p:UseAppHost=false

FROM mcr.microsoft.com/dotnet/aspnet:10.0 AS runtime
WORKDIR /app
ENV ASPNETCORE_HTTP_PORTS=8080 \
    ASPNETCORE_ENVIRONMENT=Production \
    DOTNET_EnableDiagnostics=0
# /data holds the SQLite database, firmware images and the Data Protection keys. It is a volume in Compose.
# The aspnet image ships a non-root user (APP_UID); give it the data directory and run as it.
RUN mkdir -p /data && chown "$APP_UID" /data
VOLUME /data
USER $APP_UID
COPY --from=build --chown=$APP_UID /app/publish .
EXPOSE 8080
ENTRYPOINT ["dotnet", "Yule.Server.dll"]
