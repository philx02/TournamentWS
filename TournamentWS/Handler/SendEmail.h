#pragma once

#include <boost/lexical_cast.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string.hpp>

#include <string>

namespace bpt = boost::posix_time;

void sendToSmtp(boost::asio::ip::tcp::socket &iSocket, const std::string &iData)
{
  std::string wData(iData + "\r\n");
  iSocket.send(boost::asio::buffer(wData, wData.size()));
}

void readFromSmtp(boost::asio::ip::tcp::socket &iSocket, const std::string &iExpectedResponse)
{
  boost::asio::streambuf wStreamBuffer;
  boost::asio::read_until(iSocket, wStreamBuffer, "\n");
  std::string wResponse;
  std::istream wStream(&wStreamBuffer);
  std::getline(wStream, wResponse);
  wStreamBuffer.consume(wStreamBuffer.size());
  //std::vector< std::string > wSplit;
  //boost::split(wSplit, wResponse, boost::is_any_of(" "), boost::token_compress_on);
  //if (!wSplit.empty() && wSplit.front() == iExpectedResponse)
  //{
  //  LOG4CXX_TRACE(mLogger, wResponse);
  //}
  //else
  //{
  //  LOG4CXX_WARN(mLogger, "SMTP Error: " << wResponse);
  //}
}

std::string formatDateForEmail(const bpt::ptime &iDate, const std::string &iTimezone)
{
  std::ostringstream wDateString;
  bpt::ptime::date_type wDate = iDate.date();
  wDateString << wDate.day() << " " << wDate.month() << " " << wDate.year() << " " << iDate.time_of_day() << " " << iTimezone;
  return wDateString.str();
}

void sendEmail(size_t iTournamentId, const std::string &iName, const std::string &iOpponent, size_t iPlayerId, size_t iOpponentId, size_t iPlayerScore, size_t iOpponentScore, const std::string &iGuid, const std::string &iEmail)
{
  static const std::string w220(boost::lexical_cast< std::string >(220));
  static const std::string w501(boost::lexical_cast< std::string >(501));
  static const std::string w250(boost::lexical_cast< std::string >(250));
  static const std::string w354(boost::lexical_cast< std::string >(354));
  static const std::string w221(boost::lexical_cast< std::string >(221));
  static const std::string wHelo("HELO");
  static const std::string wData("DATA");
  static const std::string wQuit("QUIT");
  static const std::string wMailFrom("MAIL FROM: ");
  static const std::string wRcpt("RCPT TO: ");
  static const std::string wFrom("From: ");
  static const std::string wTo("To: ");
  static const std::string wSubject("Subject: ");
  static const std::string wDate("Date: ");
  static const std::string wContentTypeAndCharset("Content-Type: text/html; charset=\"utf-8\"");
  static const std::string wFinish(".");
  static const std::string wFromEmail("no-reply@tws-cae.com");
  static const std::string wFromName("Tournament WebSocket <" + wFromEmail + ">");
  static boost::asio::io_service wIoService;
  try
  {
    boost::asio::ip::tcp::endpoint wSmtpServer = *boost::asio::ip::tcp::resolver(wIoService).resolve(boost::asio::ip::tcp::resolver::query("mail.cae.com", "25"));
    boost::asio::ip::tcp::socket wSocket(wIoService);
    wSocket.connect(wSmtpServer);
    readFromSmtp(wSocket, w220);
    sendToSmtp(wSocket, wHelo);
    readFromSmtp(wSocket, w501);
    sendToSmtp(wSocket, wMailFrom + wFromEmail);
    readFromSmtp(wSocket, w250);
    sendToSmtp(wSocket, wRcpt + iEmail);
    readFromSmtp(wSocket, w250);
    sendToSmtp(wSocket, wData);
    readFromSmtp(wSocket, w354);
    sendToSmtp(wSocket, wFrom + wFromName);
    sendToSmtp(wSocket, wTo + iName + " <" + iEmail + ">");
    sendToSmtp(wSocket, wSubject + "Result Submission");
    sendToSmtp(wSocket, wDate + formatDateForEmail(bpt::second_clock::universal_time(), "UT"));
    sendToSmtp(wSocket, wContentTypeAndCharset);
    std::string wMessage;
    wMessage += "<style> p { font-family: 'Trebuchet MS', Arial, Helvetica, sans-serif; } </style>";
    wMessage += "<p>Please, confirm that you have <b>";
    wMessage += iPlayerScore > iOpponentScore ? "WON" : "LOSS";
    wMessage += "</b> the matchup against " + iOpponent;
    wMessage += " by a score of <b>" + std::to_string(iPlayerScore) + " - " + std::to_string(iOpponentScore) + "</b>.</p>";
    wMessage += "<p><a href='google.com'>I agree</a></p>";
    wMessage += "<p><a href='google.com'>I disagree</a></p>";
    sendToSmtp(wSocket, wMessage);
    sendToSmtp(wSocket, wFinish);
    readFromSmtp(wSocket, w250);
    sendToSmtp(wSocket, wQuit);
    readFromSmtp(wSocket, w221);
    wSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    wSocket.close();
  }
  catch (std::exception &)
  {
  }
}