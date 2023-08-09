<?php

namespace API\Models;

use API;
use API\Core\Auth;
use API\Core\Model;
use API\Fields\BooleanField;
use API\Fields\IntegerField;
use API\Fields\StringField;

# Constants
const API_BACKUP_FILE_PATH = "/usr/local/share/pfSense-pkg-API/backup.json";
const API_BACKUP_NOT_CONFIGURED = -1;
const API_RESTORE_SUCCESS = 0;
const API_RESTORE_NO_CHANGE = 1;
const API_RESTORE_NO_BACKUP = 2;

class APISettings extends Model {
    public BooleanField $enabled;
    public BooleanField $read_only;
    public BooleanField $keep_backup;
    public BooleanField $login_protection;
    public BooleanField $log_successful_auth;
    public BooleanField $allow_pre_releases;
    public StringField $auth_mode;
    public IntegerField $jwt_exp;
    public IntegerField $key_length_bytes;
    public StringField $key_hash_algo;
    public BooleanField $ha_sync;
    public StringField $ha_sync_hosts;
    public StringField $ha_sync_username;
    public StringField $ha_sync_password;

    public function __construct(mixed $id = null, array $representation_data = [], Auth $client = null)
    {
        # Obtain the API config index and set the config path to the API configuration
        $this->id = API\Core\Tools\get_api_config()[0];
        $this->config_path = "installedpackages/package/$this->id/conf";

        # Set Fields for this Model
        $this->enabled = new BooleanField(
            default: true,
            indicates_true: "enabled",
            indicates_false: "disabled",
            help_text: "Enables or disables the API. If set to `false`, the API will no longer respond to API requests 
            and can only be re-enabled via webConfigurator."
        );
        $this->read_only = new BooleanField(
            default: false,
            indicates_true: "enabled",
            indicates_false: "disabled",
            help_text: "Enables or disables read-only API access. If enabled, the API will only respond to GET requests"
        );
        $this->keep_backup = new BooleanField(
            default: true,
            indicates_true: "enabled",
            indicates_false: "disabled",
            help_text: "Enables or disables keeping a persistent backup of the API configuration that can be used
            to restore the API configuration after package and systems updates."
        );
        $this->login_protection = new BooleanField(
            default: true,
            indicates_true: "enabled",
            indicates_false: "disabled",
            help_text: "Enables or disables Login Protection for API authentication. When enabled, Login Protection will
            monitor API attempts and temporarily block clients who fail API authentication too many times within a
            period of time. When disabled, Login Protection will not monitor API authentication but will continue to 
            monitor webConfigurator and SSH logins (if configured). Login Protection can be configured globally in
            System > Advanced."
        );
        $this->log_successful_auth = new BooleanField(
            default: false,
            indicates_true: "enabled",
            indicates_false: "disabled",
            help_text: "Enables or disables logging of API authentication attempts that are successful. By default, only
            failed API authentication attempts are logged to prevent flooding the authentication logs. This field is
            only applicable when the API `login_protection` setting is enabled."
        );
        $this->allow_pre_releases = new BooleanField(
            default: false,
            indicates_true: "enabled",
            indicates_false: "disabled",
            help_text: "Enables or disables displaying pre-releases in available API updates. Pre-releases contain fixes
            and features that are currently under development and may not be fully stable. Use of pre-release versions
            is at your own risk."
        );
        $this->auth_mode = new StringField(
            default: "local",
            choices: ["local", "jwt", "key"],
            help_text: "Sets the API authentication mode required to authenticate API calls."
        );
        $this->jwt_exp = new IntegerField(
            default: 3600,
            minimum: 300,
            maximum: 86400,
            help_text: "Sets the amount of time (in seconds) JWTs are valid for. This field is only applicable when
            `auth_mode` is set to `jwt`."
        );
        $this->key_hash_algo = new StringField(
            default: "sha256",
            choices: ["sha256", "sha384", "sha512"],
            help_text: "Sets the hash algorithm used for API keys. This field is only applicable when `auth_mode` is
            set to `key`."
        );
        $this->key_length_bytes = new IntegerField(
            default: 24,
            choices: [16, 24, 32, 64],
            help_text: "Sets the length of the API key (in bytes). Greater key lengths provide greater security, but 
            also increase the number of characters used in the key string."
        );
        $this->ha_sync = new BooleanField(
            default: false,
            indicates_true: "enabled",
            indicates_false: "disabled",
            help_text: "Enables or disables syncing API settings to HA peers. When enabled, API settings from this
            host will automatically be synced to any hosts defined in `ha_sync_hosts`."
        );
        $this->ha_sync_hosts = new StringField(
            default: [],
            allow_empty: true,
            many: true,
            many_minimum: 0,
            many_maximum: 10,
            delimiter: " ",
            help_text: "Set a list of IP addresses or hostnames to sync API settings to."
        );
        $this->ha_sync_username = new StringField(
            default: "",
            allow_empty: true,
            help_text: "Sets the username to use when authenticating for HA sync processes. This user must be the present
            on all hosts defined in `ha_sync_hosts`."
        );
        $this->ha_sync_password = new StringField(
            default: "",
            allow_empty: true,
            write_only: true,
            help_text: "Sets the password to use when authenticating for HA sync processes. This must be the password
            for the user defined in `ha_sync_username` and must be the same on all hosts defined in `ha_sync_hosts`."
        );

        parent::__construct($id, $representation_data, $client);
    }

    /**
     * Creates a backup of the current API configuration at /usr/local/share/pfSense-pkg-API/backup.json.
     * @returns integer Returns 0 when backup was successful, or -1 when the API's `keep_backup` field is disabled.
     */
    public static function backup_from_file() : int {
        # Read current API settings
        $api_settings = new APISettings();
        $api_settings->from_internal();

        # Don't allow restore if the `keep_backup` is not enabled
        if (!$api_settings->keep_backup->value) {
            return API_BACKUP_NOT_CONFIGURED;
        }

        $backup_api_conf = json_encode($api_settings->to_internal());
        file_put_contents(API_BACKUP_FILE_PATH, $backup_api_conf);
        return 0;
    }

    /**
     * Restores the API configuration using the current backup file at /usr/local/share/pfSense-pkg-API/backup.json
     * @returns integer Returns 0 when the restore was successful, 1 when there was no chanaged between the current
     * API configuration and the backup file, 2 when no backup file exists current, or -1 when the API's `keep_backup`
     * field is disabled.
     */
    public static function restore_from_backup() : int {
        # Variables
        $backup_api_conf_json = (is_file(API_BACKUP_FILE_PATH)) ? file_get_contents(API_BACKUP_FILE_PATH) : "";
        $backup_api_conf = ($backup_api_conf_json) ? json_decode($backup_api_conf_json, true) : [];

        # Read current API settings
        $api_settings = new APISettings();
        $api_settings->from_internal();
        
        # Don't allow restore if the `keep_backup` is not enabled
        if (!$api_settings->keep_backup->value) {
            return API_BACKUP_NOT_CONFIGURED;
        }

        # Don't allow restore if there is no backup present
        if (!$backup_api_conf) {
            return API_RESTORE_NO_BACKUP;
        }

        # Don't allow restore if there is no change to backup
        if ($api_settings->to_internal() === $backup_api_conf) {
            return API_RESTORE_NO_CHANGE;
        }

        config_set_path("installedpackages/package/$api_settings->id/conf", $backup_api_conf);
        write_config("Synchronized persistent API configuration");
        return API_RESTORE_SUCCESS;
    }

    /**
     * Refresh the API backup file and sync changes to API clients if configured.
     */
    public function apply()
    {
        parent::apply(); // TODO: Change the autogenerated stub
    }
}