Init_submodule()

Link_dependencies(
  oxbox-utilities   # text: the media type's key folding; span: the octet cast
  boost::boost      # asio's coroutine sockets, beast's HTTP/1.1 message syntax
  openssl::openssl  # boost::asio::ssl owns no crypto: this is its engine
)
