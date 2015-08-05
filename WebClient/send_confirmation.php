<?
$command = "echo \"nc ".$_GET["server"]." ".$_GET["port"]." <<< $'".$_GET["string"]."\\n'\" | bash";
system($command, $a);
echo "Confirmation sent."
?>