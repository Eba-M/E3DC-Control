<?php
$datei = "e3dc.wallbox.txt";
$zeile = "1";

$zeile3 = $_POST["zwei"];
if  (empty($zeile3)) $zeile3 = "2";

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
    if ($zeile<"1")
    {
            echo "es wurden keine Ladezeiten geplant<br>";
    }
    else
    {
            if ($zeile<"2")
                    echo "Die neue  Ladedauer beträgt eine  Stunde<br>";
            if ($zeile >= "2")
                    echo "Die geplante  Ladedauer beträgt " . $zeile .  " Stunden<br>";
            if (! empty($zeile2))
                    echo "die geplante Ladezeiten sind:<br><br> " . $zeile2 ."<br>";
}
            while  (! feof($myfile))
                    {$zeile2 = fgets($myfile);
                    echo $zeile2 ."<br>";}
            fclose($myfile);

    
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

