<?php
if (isset($_GET['__phide_mtime'])) {
    header('Content-Type: text/plain; charset=utf-8');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    clearstatcache();
    $dir = getcwd();
    $maxMtime = 0;
    if (is_dir($dir)) {
        try {
            $it = new RecursiveIteratorIterator(
                new RecursiveDirectoryIterator($dir, FilesystemIterator::SKIP_DOTS),
                RecursiveIteratorIterator::SELF_FIRST
            );
            foreach ($it as $file) {
                if ($file->isFile()) {
                    $ext = strtolower($file->getExtension());
                    if (in_array($ext, ['php', 'phtml', 'html', 'htm', 'css', 'js', 'json', 'sql'], true)) {
                        $m = @$file->getMTime();
                        if ($m > $maxMtime) {
                            $maxMtime = $m;
                        }
                    }
                }
            }
        } catch (Throwable $e) {
        }
    }
    echo (string)$maxMtime;
    exit;
}
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');
header('Expires: 0');
