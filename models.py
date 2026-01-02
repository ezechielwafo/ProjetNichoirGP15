from sqlalchemy.orm import DeclarativeBase, mapped_column, sessionmaker, relationship
from sqlalchemy import Integer, String, DateTime, Float, ForeignKey, create_engine, func

class Base(DeclarativeBase):
    pass

# ---------- TABLE dataBattery ----------
class DataBattery(Base):
    __tablename__ = "dataBattery"

    id = mapped_column(Integer, primary_key=True, autoincrement=True)
    batteryLevel = mapped_column(String(255), nullable=True)  # "85%"
    voltage = mapped_column(Float, nullable=True)
    percentage = mapped_column(Integer, nullable=True)
    device_id = mapped_column(String(100), nullable=True)
    submissionDate = mapped_column(DateTime, server_default=func.now())

    # Relation inverse (optionnelle)
    images = relationship("DataImage", back_populates="battery")

# ---------- TABLE dataImage ----------
class DataImage(Base):
    __tablename__ = "dataImage"

    id = mapped_column(Integer, primary_key=True, autoincrement=True)
    imageName = mapped_column(String(100), nullable=False)
    url = mapped_column(String(255), nullable=False)
    submissionDate = mapped_column(DateTime, server_default=func.now())

    # Nouveaux champs (à ajouter à la table existante)
    id_battery = mapped_column(Integer, ForeignKey('dataBattery.id'), nullable=True)
    trigger_type = mapped_column(String(50), nullable=True)
    boot_count = mapped_column(Integer, nullable=True)
    pir_count = mapped_column(Integer, nullable=True)

    # Relation
    battery = relationship("DataBattery", back_populates="images")

# ---------- CREATION DES TABLES ----------
def main():
    engine = create_engine("mariadb+mariadbconnector://kouatche:0123456789@localhost:3306/smartCities")

    # ⚠️ Utilisez create_all() avec précaution : cela crée SEULEMENT les tables manquantes
    # Si les tables existent déjà, elles ne seront PAS modifiées
    Base.metadata.create_all(engine)

    Session = sessionmaker(bind=engine)
    session = Session()

    # Test d'insertion
    nouvelle_image = DataImage(
        imageName="imgChat",
        url="https://s1.1zoom.me/b5050/338/Cats_Kittens_Two_Glance_486349_1366x768.jpg"
    )
    session.add(nouvelle_image)
    session.commit()
    print(f"Image insérée avec id {nouvelle_image.id} et submissionDate {nouvelle_image.submissionDate}")

if __name__ == "__main__":
    main()