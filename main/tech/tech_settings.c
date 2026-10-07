#include "tech_settings.h"
#include <string.h>
#include "nvs.h"

#define TECH_SETTINGS_NAMESPACE "tech_terminal"
#define TECH_SETTINGS_KEY "prefs"
#define TECH_SETTINGS_SCHEMA 2

static bool settings_valid(const tech_settings_t *settings);
static nvs_handle_t settings_handle;
static tech_settings_t current_settings;
static bool settings_ready;

typedef struct {
    uint16_t schema_version;
    uint8_t brightness;
    bool dark_theme;
    bool time_format_24h;
    uint16_t refresh_seconds;
    char browser_home[128];
    char bookmarks[TECH_MAX_BOOKMARKS][128];
    uint8_t bookmark_count;
    char github_owner[64];
    char github_repos[TECH_MAX_GITHUB_REPOS][64];
    uint8_t github_repo_count;
    char f1_driver[24];
    char f1_team[32];
    char information_city[48];
    uint8_t pi_count;
    tech_pi_device_t pi_devices[TECH_MAX_PI_DEVICES];
} tech_settings_v1_t;

static void settings_defaults(tech_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->schema_version = TECH_SETTINGS_SCHEMA;
    settings->brightness = 80;
    settings->screen_timeout_minutes = 0;
    settings->dark_theme = true;
    settings->time_format_24h = true;
    settings->refresh_seconds = 300;
    strlcpy(settings->browser_home, "https://example.com/", sizeof(settings->browser_home));
}

static void settings_migrate_v1(const tech_settings_v1_t *old)
{
    memset(&current_settings, 0, sizeof(current_settings));
    current_settings.schema_version = TECH_SETTINGS_SCHEMA;
    current_settings.brightness = old->brightness;
    current_settings.screen_timeout_minutes = 0;
    current_settings.dark_theme = old->dark_theme;
    current_settings.time_format_24h = old->time_format_24h;
    current_settings.refresh_seconds = old->refresh_seconds;
    memcpy(current_settings.browser_home, old->browser_home, sizeof(old->browser_home));
    memcpy(current_settings.bookmarks, old->bookmarks, sizeof(old->bookmarks));
    current_settings.bookmark_count = old->bookmark_count;
    memcpy(current_settings.github_owner, old->github_owner, sizeof(old->github_owner));
    memcpy(current_settings.github_repos, old->github_repos, sizeof(old->github_repos));
    current_settings.github_repo_count = old->github_repo_count;
    memcpy(current_settings.f1_driver, old->f1_driver, sizeof(old->f1_driver));
    memcpy(current_settings.f1_team, old->f1_team, sizeof(old->f1_team));
    memcpy(current_settings.information_city, old->information_city, sizeof(old->information_city));
    current_settings.pi_count = old->pi_count;
    memcpy(current_settings.pi_devices, old->pi_devices, sizeof(old->pi_devices));
}

esp_err_t tech_settings_init(void)
{
    esp_err_t err = nvs_open(TECH_SETTINGS_NAMESPACE, NVS_READWRITE, &settings_handle);
    if (err != ESP_OK) return err;

    uint8_t stored_blob[sizeof(current_settings)] = {0};
    size_t stored_size = sizeof(stored_blob);
    err = nvs_get_blob(settings_handle, TECH_SETTINGS_KEY,
                       stored_blob, &stored_size);
    uint16_t stored_schema = 0;
    if (err == ESP_OK && stored_size >= sizeof(stored_schema)) {
        memcpy(&stored_schema, stored_blob, sizeof(stored_schema));
    }

    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH) {
        settings_defaults(&current_settings);
        err = nvs_set_blob(settings_handle, TECH_SETTINGS_KEY,
                           &current_settings, sizeof(current_settings));
        if (err == ESP_OK) err = nvs_commit(settings_handle);
    } else if (err == ESP_OK && stored_schema == 1 &&
               stored_size == sizeof(tech_settings_v1_t)) {
        tech_settings_v1_t legacy;
        memcpy(&legacy, stored_blob, sizeof(legacy));
        settings_migrate_v1(&legacy);
        if (!settings_valid(&current_settings)) settings_defaults(&current_settings);
        err = nvs_set_blob(settings_handle, TECH_SETTINGS_KEY,
                           &current_settings, sizeof(current_settings));
        if (err == ESP_OK) err = nvs_commit(settings_handle);
    } else if (err == ESP_OK && stored_schema == TECH_SETTINGS_SCHEMA &&
               stored_size == sizeof(current_settings)) {
        memcpy(&current_settings, stored_blob, sizeof(current_settings));
        if (!settings_valid(&current_settings)) {
            settings_defaults(&current_settings);
            err = nvs_set_blob(settings_handle, TECH_SETTINGS_KEY,
                               &current_settings, sizeof(current_settings));
            if (err == ESP_OK) err = nvs_commit(settings_handle);
        }
    } else if (err == ESP_OK) {
        settings_defaults(&current_settings);
        err = nvs_set_blob(settings_handle, TECH_SETTINGS_KEY,
                           &current_settings, sizeof(current_settings));
        if (err == ESP_OK) err = nvs_commit(settings_handle);
    }
    if (err == ESP_OK) settings_ready = true;
    else nvs_close(settings_handle);
    return err;
}
esp_err_t tech_settings_get(tech_settings_t *out)
{
    if (!settings_ready || out == NULL) return ESP_ERR_INVALID_STATE;
    *out = current_settings;
    return ESP_OK;
}

static bool field_terminated(const char *field, size_t capacity)
{
    return memchr(field, '\0', capacity) != NULL;
}

static bool settings_valid(const tech_settings_t *settings)
{
    if (settings == NULL || settings->pi_count > TECH_MAX_PI_DEVICES ||
        settings->bookmark_count > TECH_MAX_BOOKMARKS ||
        settings->github_repo_count > TECH_MAX_GITHUB_REPOS ||
        settings->brightness < 1 || settings->brightness > 100 ||
        (settings->schema_version >= 2 && settings->screen_timeout_minutes > 120) ||
        settings->refresh_seconds < 5 || settings->refresh_seconds > 86400) {
        return false;
    }
    if (!field_terminated(settings->browser_home, sizeof(settings->browser_home)) ||
        !field_terminated(settings->github_owner, sizeof(settings->github_owner)) ||
        !field_terminated(settings->f1_driver, sizeof(settings->f1_driver)) ||
        !field_terminated(settings->f1_team, sizeof(settings->f1_team)) ||
        !field_terminated(settings->information_city, sizeof(settings->information_city))) {
        return false;
    }
    for (int i = 0; i < TECH_MAX_BOOKMARKS; i++) {
        if (!field_terminated(settings->bookmarks[i], sizeof(settings->bookmarks[i]))) return false;
    }
    for (int i = 0; i < TECH_MAX_GITHUB_REPOS; i++) {
        if (!field_terminated(settings->github_repos[i], sizeof(settings->github_repos[i]))) return false;
    }
    for (int i = 0; i < TECH_MAX_PI_DEVICES; i++) {
        if (!field_terminated(settings->pi_devices[i].name, sizeof(settings->pi_devices[i].name)) ||
            !field_terminated(settings->pi_devices[i].base_url, sizeof(settings->pi_devices[i].base_url)) ||
            !field_terminated(settings->pi_devices[i].token, sizeof(settings->pi_devices[i].token))) return false;
    }
    return true;
}

esp_err_t tech_settings_set(const tech_settings_t *settings)
{
    if (!settings_ready) return ESP_ERR_INVALID_STATE;
    if (!settings_valid(settings)) return ESP_ERR_INVALID_ARG;

    tech_settings_t next = *settings;
    next.schema_version = TECH_SETTINGS_SCHEMA;
    esp_err_t err = nvs_set_blob(settings_handle, TECH_SETTINGS_KEY,
                                 &next, sizeof(next));
    if (err == ESP_OK) err = nvs_commit(settings_handle);
    if (err == ESP_OK) current_settings = next;
    return err;
}
