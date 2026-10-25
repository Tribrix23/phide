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
$cfg['Servers'][$i]['host'] = 'localhost';
$cfg['Servers'][$i]['port'] = '3306';
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
$cfg['UploadDir'] = '';
$cfg['SaveDir'] = '';
$cfg['SendErrorReports'] = 'never';
$cfg['Console']['Mode'] = 'collapse';
