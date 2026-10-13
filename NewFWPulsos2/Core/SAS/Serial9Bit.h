/*
 * Serial9Bit.h
 *
 *  COMUNICACION SERIAL A 9 BITS.  Interfaz abstracta pensada como el PuertoSerie
 *  (8 bits) del proyecto, pero NO son intercambiables: sus metodos se llaman
 *  parecido y hacen cosas distintas.  Por eso los de esta interfaz terminan en
 *  "9bit": si ves el sufijo, es el puerto de 9 bits; si no lo ves, es PuertoSerie.
 *
 *  ===========================================================================
 *   PuertoSerie (USART1, 8 bits)   frente a   Serial9Bit (USART2, 9 bits, SAS)
 *  ===========================================================================
 *   PuertoSerie                     Serial9Bit               Que hace / diferencia
 *  ---------------------------------------------------------------------------
 *   init(&huart)                    init(&huart)             Guarda el UART y arma la recepcion
 *                                   (en Serial9BitStm32)     por DMA.  Igual en los dos.
 *  ---------------------------------------------------------------------------
 *   write(uint8_t*, n)              write9bit(uint16_t*, n)  Envia datos.
 *     devuelve bool                   devuelve CUANTAS       - 8 bits: BYTES; hace COLA de 512 y
 *                                     palabras acepto          nunca rechaza (salvo cola llena).
 *                                                            - 9 bits: PALABRAS de 16 bits (el 9o
 *                                                              bit va en el bit 8).  SIN cola: si
 *                                                              el envio anterior sigue en curso,
 *                                                              RECHAZA y devuelve 0 (max. 256).
 *  ---------------------------------------------------------------------------
 *   available()                     available9bit()          Cuantos datos hay sin leer.
 *     devuelve BYTES                  devuelve PALABRAS      El de 9 bits ademas re-arma la
 *                                                            recepcion tras un error de linea.
 *  ---------------------------------------------------------------------------
 *   readBuffer(buf, size)           readBuffer9bit(dest,     Saca lo recibido.
 *     size en BYTES                   bytes, marcas)         - 8 bits: entra y sale la misma
 *     devuelve BYTES                  bytes en tamano,         unidad (bytes).
 *                                     devuelve PALABRAS      - 9 bits: el tamano va en BYTES pero
 *                                                              devuelve PALABRAS; 'marcas' (opcional)
 *                                                              trae la hora de llegada en ms.
 *  ---------------------------------------------------------------------------
 *   flush()                         flush9bit()              !! SON LO CONTRARIO !!
 *     BORRA lo recibido sin leer      ESPERA a que termine   - PuertoSerie::flush() tira el RX
 *                                     la transmision           pendiente: NO usarlo despues de write.
 *                                     (tope de 100 ms)       - flush9bit() no toca el RX; es el
 *                                                              flush de Arduino.
 *  ---------------------------------------------------------------------------
 *   read()                          (no existe)              Lee un solo byte.
 *   Print() / Println()             (no existen)             Texto formateado.
 *   setBaudRate()                   (no existe)              El SAS es siempre 19200.
 *  ---------------------------------------------------------------------------
 *   handleIdleIRQ(Size)             manejarRxEvent(Size)     Aviso del HAL: llegaron datos.
 *   handleTxComplete()              manejarTxCompleto()      Aviso del HAL: termino de enviar.
 *   (lo hace main.cpp a mano)       manejarError()           Aviso del HAL: error de linea.
 *  ===========================================================================
 *
 *  Es lo unico que la clase SasEgm necesita del hardware.  No sabe nada de
 *  SAS: solo mueve palabras de 9 bits (el noveno bit viaja en el bit 8, 0x100).
 *  Portar el SAS a otro micro o a una PC es escribir una clase que implemente
 *  estos cuatro metodos.
 *
 *      write9bit(palabras, n)                  Envia n palabras de 9 bits
 *      available9bit()                         Palabras recibidas sin leer
 *      readBuffer9bit(destino, tamano, marcas) Todo lo pendiente de una vez
 *      flush9bit()                             Espera a que termine la transmision
 *
 *  Uso:
 *
 *      uint16_t buf[256];
 *      if (serial.available9bit()) {
 *          n = serial.readBuffer9bit((uint8_t*)buf, sizeof(buf));   // palabras leidas
 *          serial.write9bit(buf, n);                                // el 9o bit viaja
 *          serial.flush9bit();
 *      }
 *
 *  readBuffer9bit: 'destino' recibe palabras de 16 bits (el llamador pasa su arreglo
 *  de uint16_t convertido a uint8_t*) y 'tamano' es su tamano EN BYTES.  Devuelve
 *  cuantas PALABRAS copio (0 si no hay).  No espera.  'marcas' es opcional
 *  (nullptr): recibe la hora de llegada de cada palabra en ms, y el SAS la usa
 *  para descartar un poleo viejo aunque el lazo principal se haya atrasado.
 *
 *  No hay begin(): cada plataforma se prepara con su propio init(...), que
 *  recibe lo que esa plataforma necesita (en STM32, el UART) y por eso no va en
 *  esta interfaz.  Las interrupciones tampoco: las llena cada plataforma.
 *
 *  La recepcion la llena la interrupcion de cada plataforma y la vacia el lazo
 *  principal: una sola escritora y una sola lectora, sin secciones criticas.
 */

#ifndef SRC_SERIAL9BIT_H_
#define SRC_SERIAL9BIT_H_

#include <stdint.h>
#include <stddef.h>

class Serial9Bit {
public:
    virtual size_t write9bit(const uint16_t* palabras, size_t largo) = 0;
    virtual size_t available9bit() = 0;
    virtual size_t readBuffer9bit(uint8_t* destino, size_t tamano, uint32_t* marcas = nullptr) = 0;

    // Espera a que termine de transmitir, con tope (100 ms en Serial9BitStm32):
    // NO es un bloqueo indefinido.  SasEgm no la llama nunca -enviarTramaSas()
    // usa write9bit(), que no espera-, asi que servicio() nunca se cuelga aqui.
    // Es para quien quiera esperar a mano fuera del lazo del SAS (ver el
    // ejemplo de arriba).
    virtual void   flush9bit() = 0;

protected:
    ~Serial9Bit() = default;   // nadie borra una serial a traves de la interfaz
};

#endif /* SRC_SERIAL9BIT_H_ */
