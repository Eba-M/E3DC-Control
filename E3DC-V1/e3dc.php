<?php
$datei = "/home/pi/E3DC-Control/e3dc.wallbox.txt";
$zeile = "1";

$zeile3 = $_POST["zwei"];
if  (empty($zeile3)) $zeile3 = "0";


$myfile = fopen($datei,"w");
if ($myfile)
{
        $zeile = $zeile3;
        fputs($myfile,$zeile3);
        fclose($myfile);
        sleep(2);


        $myfile = fopen($datei,"r") ;
        $zeile = fgets($myfile);
        if  (! feof($myfile))    $zeile2 = fgets($myfile);
        fclose($myfile);
        if  (empty($zeile3))
        {
                echo "Es ist keine Ladung geplant<br>";
        }
        if  ($zeile3 == "1")
        {
                echo "Die neue Ladedauer beträgt eine  Stunde<br>";
                echo "die geplante Ladezeite ist  um " . $zeile2 . " Börsenpreis<br>";
        }
        if  ( $zeile3 > "1")
        {
                echo "Die neue Ladedauer beträgt " . $zeile3 .  " Stunden<br>";
                echo "die geplante Ladezeiten sind um " . $zeile2 . " Börsenpreis<br>";
        }
}

?>
<html>
 <head>
  <title>PHP-Test</title>
 </head>
 <body>
<form action="<?php $_PHP_SELF ?>"  method="post">
    <p><input type="text" name="zwei" value=<?=$zeile?> minlength="1" maxlength="2" size="2">  Stunden Ladedauer </p>
    <p><input type="submit"></p>
</form>

 </body>
</html>

