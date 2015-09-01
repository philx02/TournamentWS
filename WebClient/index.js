var wWebSocket;

$(document).on('keydown', function (e)
{
  if (e.keyCode === 27) // ESC
  {
    div_hide();
  }
});

function startup()
{
  connect();
}

function connect()
{
  try
  {
    wWebSocket = new WebSocket(QueryString.server);
  }
  catch (exc)
  {
    console.log(exc.message);
  }
  wWebSocket.onopen = function (evt) { greetings(); };
  wWebSocket.onmessage = function (evt) { updateTable(evt); };
  wWebSocket.onerror = function (evt) { try_reconnect(evt); };
  wWebSocket.onclose = function (evt) { try_reconnect(evt); };
}

function try_reconnect(evt)
{
  if (wWebSocket.readyState != 0 && wWebSocket.readyState != 1)
  {
    document.getElementById("disconnect_image").innerHTML = "<img src='disconnected-128.png' height='40' width='40'/>";
    connect();
    setTimeout(try_reconnect, 5000, evt);
  }
}

function greetings()
{
  wWebSocket.send("get|" + QueryString.id);
}

function updateTable(evt)
{
  document.getElementById("disconnect_image").innerHTML = "";
  var wSplit = evt.data.split("|");
  document.getElementById("tournament_name").innerHTML = wSplit[0];
  document.title = wSplit[0];
  var wPlayersCouple = wSplit[1].split(";");
  wPlayersCouple.pop();
  var wTable = wSplit[2].split(";");
  var wHtml = "<table id='result_table'>";
  wHtml += "<tr><th></th>";
  var wPlayerIds = [];
  for (i = 0; i < wPlayersCouple.length; ++i)
  {
    var wPlayerSplit = wPlayersCouple[i].split(",");
    wHtml += "<th>" + wPlayerSplit[1] + "</th>";
    wPlayerIds.push({ id: wPlayerSplit[0], name: wPlayerSplit[1] });
  }
  for (i = 0; i < wPlayersCouple.length; ++i)
  {
    var wPlayerSplit = wPlayersCouple[i].split(",");
    wHtml += "<tr><td class='names'>" + wPlayerSplit[1] + "</td>"
    var wRow = wTable[i].split(",");
    for (j = 0; j < wPlayersCouple.length; ++j)
    {
      if (i != j)
      {
        var wResult;
        var wClass;
        if (wRow[j] == "")
        {
          wResult = "<img id='edit_result' onclick='submitResult(" + wPlayerIds[j].id + ", \"" + wPlayerIds[j].name + "\", " + wPlayerIds[i].id + ", \"" + wPlayerIds[i].name + "\")' src='edit.png' height='20' width='20'/>";
          wClass = "unfinished";
        }
        else
        {
          var wResultSplit = wRow[j].split(".");
          wResult = wResultSplit[0];
          wClass = wResultSplit[1] >= 2 ? (wResult[0] == 2 ? "victory" : wResult[4] == 2 ? "defeat" : "unfinished") : "unfinished";
        }
        wHtml += "<td class='" + wClass + "'>" + wResult + "</td>";
      }
      else
      {
        wHtml += "<td class='invalid'>&nbsp;</td>";
      }
    }
  }
  wHtml += "</tr>";
  wHtml += "<tr>";
  wHtml += "<td class='score'>Score</td>"
  var wPlayersScores = wSplit[3].split(";");
  wPlayersScores.pop();
  for (i = 0; i < wPlayersScores.length; ++i)
  {
    wHtml += "<td>" + wPlayersScores[i] + "</td>";
  }
  wHtml += "</tr>";
  wHtml += "</table>";
  document.getElementById("tournament_table").innerHTML = wHtml;

  var wLeaderboardComment = (wSplit[4] != 0) ? (wSplit[4] + " matches to go") : "Final";
  document.getElementById("leaderboard_title").innerHTML = "Leaderboard (" + wLeaderboardComment + ")";

  wHtml = "<table id='leader_board_table'>";
  var wLeaderBoard = wSplit[5].split(";");
  wLeaderBoard.pop();
  for (i = 0; i < wLeaderBoard.length; ++i)
  {
    var wPlayerScore = wLeaderBoard[i].split(",");
    wHtml += "<tr><td class='rank'>" + (i + 1) + "</td><td>" + wPlayerScore[0] + "</td><td class='score'>" + wPlayerScore[1] + "</td><td>(" + wPlayerScore[2] + ")</td></tr>";
  }
  wHtml += "</table>";
  document.getElementById("leader_board").innerHTML = wHtml;
}

function submitResult(iPlayer1Id, iPlayer1Name, iPlayer2Id, iPlayer2Name)
{
  document.getElementById("form_player1_id").value = iPlayer1Id;
  document.getElementById("form_player1_name").innerHTML = iPlayer1Name;
  document.getElementById("form_player2_id").value = iPlayer2Id;
  document.getElementById("form_player2_name").innerHTML = iPlayer2Name;
  document.getElementById("form_submission_error").innerHTML = "";
  document.getElementById("form").reset();
  div_show();
  document.getElementById("form_score1").focus();
}

function div_show()
{
  document.getElementById('result_submission_div').style.display = "block";
}

function div_hide()
{
  document.getElementById('result_submission_div').style.display = "none";
}

function getConfigurationUrl()
{
  var wUrl = [location.protocol, '//', location.host, location.pathname].join('');
  wUrl = wUrl.replace("index.html", "");
  if (wUrl.slice(-1) != "/")
  {
    wUrl += "/";
  }
  wUrl += "confirmation.html";
  return wUrl;
}

function submit_result()
{
  if (!$.isNumeric(document.getElementById("form_score1").value) || !$.isNumeric(document.getElementById("form_score2").value))
  {
    document.getElementById("form_submission_error").innerHTML = "Non-numeric entry";
    return;
  }
  var wScorePlayer1 = parseInt(document.getElementById("form_score1").value);
  var wScorePlayer2 = parseInt(document.getElementById("form_score2").value);
  if (wScorePlayer1 + wScorePlayer2 > 3 || wScorePlayer1 != 2 && wScorePlayer2 != 2)
  {
    document.getElementById("form_submission_error").innerHTML = "Invalid entries for a best of 3";
    return;
  }
  div_hide();
  var wSubmission = "submit|" + QueryString.id + "|";
  wSubmission += document.getElementById("form_player1_id").value + ";";
  wSubmission += document.getElementById("form_score1").value + ";";
  wSubmission += document.getElementById("form_player2_id").value + ";";
  wSubmission += document.getElementById("form_score2").value;
  wSubmission += "|" + getConfigurationUrl() + "|" + QueryString.server;
  wWebSocket.send(wSubmission);
}
