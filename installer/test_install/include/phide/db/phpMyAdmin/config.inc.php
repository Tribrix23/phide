<?php
/**
 * phpMyAdmin configuration managed by Phide IDE.
 */

declare(strict_types=1);

/**
 * 32-bytes secret for cookie authentication encryption.
 */
$cfg['blowfish_secret'] = 'phide_pma_aes256_blowfish_secret_key_32b!';

/**
 * Servers configuration
 */
$i = 0;

/**
 * Primary Database Server (MySQL / MariaDB)
 */
$i++;
$cfg['Servers'][$i]['auth_type'] = 'config'; // 'config' for seamless direct login, or 'cookie'
$cfg['Servers'][$i]['host'] = '127.0.0.1';

/**
 * Dynamic MySQL Port resolution:
 * Checks environment variable PHIDE_MYSQL_PORT first, then mysql_port.txt in PMA directory,
 * then ini mysqli.default_port, falling back to 3306.
 * Allows phpMyAdmin to seamlessly connect to dynamic port (3307+) when 3306 is taken.
 */
$mysqlPort = getenv('PHIDE_MYSQL_PORT');
if (!$mysqlPort && file_exists(__DIR__ . DIRECTORY_SEPARATOR . 'mysql_port.txt')) {
    $filePort = trim((string)@file_get_contents(__DIR__ . DIRECTORY_SEPARATOR . 'mysql_port.txt'));
    if (is_numeric($filePort) && (int)$filePort > 0) {
        $mysqlPort = $filePort;
    }
}
if (!$mysqlPort && function_exists('ini_get')) {
    $iniPort = ini_get('mysqli.default_port');
    if ($iniPort && is_numeric($iniPort) && (int)$iniPort > 0) {
        $mysqlPort = $iniPort;
    }
}
if (!$mysqlPort || !is_numeric($mysqlPort)) {
    $mysqlPort = '3306';
}
$cfg['Servers'][$i]['port'] = (string)$mysqlPort; // '3306'
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['extension'] = 'mysqli';
$cfg['Servers'][$i]['compress'] = false;
$cfg['Servers'][$i]['user'] = 'root';
$cfg['Servers'][$i]['password'] = '';
$cfg['Servers'][$i]['AllowNoPassword'] = true;

/**
 * phpMyAdmin storage and features
 */
$cfg['Servers'][$i]['controluser'] = '';
$cfg['Servers'][$i]['controlpass'] = '';
$cfg['Servers'][$i]['pmadb'] = '';
$cfg['ZeroConf'] = false;
$cfg['UploadDir'] = '';
$cfg['SaveDir'] = '';
$cfg['SendErrorReports'] = 'never';
$cfg['Console']['Mode'] = 'collapse';
